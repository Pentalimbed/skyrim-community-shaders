/// By ProfJack/五脚猫, 2024-2-17 UTC

#include "common.hlsli"

#include "TriDither.fx"

RWTexture2D<float4> RWTexOut : register(u0);

Texture2D<float4> TexColor : register(t0);
Texture1D<float> RWTexAdaptation : register(t1);

cbuffer TonemapCB : register(b0)
{
	float2 AdaptationRange;

	float ExposureCompensation;

	float AgXSlope;
	float AgXPower;
	float AgXOffset;
	float AgXSaturation;

	float PurkinjeStartEV;
	float PurkinjeMaxEV;
	float PurkinjeStrength;

	uint EnableAutoExposure;
	uint DitherMode;

	float Timer;
};

SamplerState SampColor
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Mirror;
	AddressV = Mirror;
	AddressW = Mirror;
};

// https://www.shadertoy.com/view/MslGR8
// method by CeeJayDK
float3 DitherShift(float3 color, uint2 pxCoord)
{
	//note: from comment by CeeJayDK
	float dither_bit = 8.0;  //Bit-depth of display. Normally 8 but some LCD monitors are 7 or even 6-bit.

	//Calculate grid position
	float grid_position = frac(dot(pxCoord, float2(1.0 / 16.0, 10.0 / 36.0) + 0.093));

	//Calculate how big the shift should be
	float dither_shift = 0.25 * (1.0 / (pow(2.0, dither_bit) - 1.0));

	//Shift the individual colors differently, thus making it even harder to see the dithering pattern
	float3 dither_shift_RGB = float3(dither_shift, -dither_shift, dither_shift);  //subpixel dithering

	//modify shift acording to grid position.
	dither_shift_RGB = lerp(2.0 * dither_shift_RGB, -2.0 * dither_shift_RGB, grid_position);  //shift acording to grid position.

	//shift the color by dither_shift
	return color + 0.5 / 255.0 + dither_shift_RGB;
}

[numthreads(32, 32, 1)] void main(uint2 tid : SV_DispatchThreadID) {
	const static float logEV = -3;  // log2(0.125)

	uint2 dims;
	RWTexOut.GetDimensions(dims.x, dims.y);
	float2 uv = tid / dims;

	float3 color = TexColor[tid].rgb;

	// auto exposure
	if (EnableAutoExposure) {
		float avgLuma = RWTexAdaptation.Load(0);
		color *= 0.18 * ExposureCompensation / clamp(avgLuma, AdaptationRange.x, AdaptationRange.y);

		// purkinje shift
		if (PurkinjeStrength > 1e-3) {
			float purkinjeMix = lerp(PurkinjeStrength, 0.f, saturate((log2(avgLuma) - logEV - PurkinjeMaxEV) / (PurkinjeStartEV - PurkinjeMaxEV)));
			if (purkinjeMix > 1e-3)
				color = PurkinjeShift(color, purkinjeMix);
		}
	} else
		color *= ExposureCompensation;

	color = Agx(color);
	color = ASC_CDL(color, AgXSlope, AgXPower, AgXOffset);
	color = Saturate(color, AgXSaturation);
	color = AgxEotf(color);

	if (DitherMode == 1)
		color = DitherShift(color, tid);
	else if (DitherMode == 2)
		color += TriDither(color, uv, Timer, 8);

	color = saturate(color);

	RWTexOut[tid] = float4(color, 1);
}