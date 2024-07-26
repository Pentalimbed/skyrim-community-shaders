/// By ProfJack/五脚猫, 2024-2-17 UTC
/// ref:
/// https://bruop.github.io/exposure/
/// https://knarkowicz.wordpress.com/2016/01/09/automatic-exposure/

#include "common.hlsli"

RWTexture1D<uint> RWTexHistogram : register(u0);
RWTexture1D<float> RWTexAdaptation : register(u1);

Texture2D<float4> TexColor : register(t0);

SamplerState SampColor
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Clamp;
	AddressV = Clamp;
	AddressW = Clamp;
};

cbuffer AutoExposureCB : register(b0)
{
	float AdaptLerp;

	float2 AdaptArea;

	float MinLogLum;
	float LogLumRange;
	float RcpLogLumRange;
};

groupshared uint histogramShared[256];

#ifdef AVG
[numthreads(256, 1, 1)] void main(uint gidx : SV_GroupIndex) {
	uint2 dims;
	TexColor.GetDimensions(dims.x, dims.y);
	uint numPixels = dims.x * dims.y * AdaptArea.x * AdaptArea.y;

	// init
	uint pixelsInBin = RWTexHistogram[gidx];
	histogramShared[gidx] = pixelsInBin * gidx;
	RWTexHistogram[gidx] = 0;  // for next frame

	GroupMemoryBarrierWithGroupSync();

	// sum
	[unroll] for (uint cutoff = (256 >> 1); cutoff > 0; cutoff >>= 1)
	{
		if (gidx < cutoff)
			histogramShared[gidx] += histogramShared[gidx + cutoff];
		GroupMemoryBarrierWithGroupSync();
	}

	// average
	if (gidx == 0) {
		// pixelsInBin here is number of zero value pixels
		float logAvgLum = (float(histogramShared[0]) / max(numPixels, 1.0)) - 1.0;
		float avgLum = exp2(((logAvgLum / 254.0) * LogLumRange) + MinLogLum);
		float adaptedLum = lerp(max(1e-5, RWTexAdaptation.Load(0)), avgLum, AdaptLerp);
		RWTexAdaptation[0] = adaptedLum;
	}
}
#else
[numthreads(16, 16, 1)] void main(uint2 tid : SV_DispatchThreadID, uint gidx : SV_GroupIndex) {
	uint2 dims;
	TexColor.GetDimensions(dims.x, dims.y);

	// init
	histogramShared[gidx] = 0;

	GroupMemoryBarrierWithGroupSync();

	// local histo
	float4 box = float4(.5 - AdaptArea * .5, .5 + AdaptArea * .5);
	if (tid.x > dims.x * box.r &&
		tid.x < dims.x * box.b &&
		tid.y > dims.y * box.g &&
		tid.y < dims.y * box.a) {
		uint bin = 0;

		float3 color = TexColor[tid].rgb;
		float luma = Luma(color);
		if (luma > 1e-10) {
			float logLuma = saturate((log2(luma) - MinLogLum) * RcpLogLumRange);
			bin = uint(lerp(1, 255, logLuma));
		}

		InterlockedAdd(histogramShared[bin], 1);
	}

	GroupMemoryBarrierWithGroupSync();

	// save to texture
	InterlockedAdd(RWTexHistogram[gidx], histogramShared[gidx]);
}
#endif