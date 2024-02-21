/// By ProfJack/五脚猫, 2024-2-17 UTC

#include "common.hlsli"

RWTexture2D<float4> RWTexOut : register(u0);

Texture2D<float4> TexColor : register(t0);
Texture1D<float> RWTexAdaptation : register(t1);

cbuffer TonemapCB : register(b0)
{
	float2 AdaptationRange;

	float KeyValue;
	float ExposureCompensation;

	float AgXSlope;
	float AgXPower;
	float AgXOffset;
	float AgXSaturation;

	float PurkinjeStartEV;
	float PurkinjeMaxEV;
	float PurkinjeStrength;

	uint EnableAutoExposure;
};

SamplerState SampColor
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Mirror;
	AddressV = Mirror;
	AddressW = Mirror;
};

[numthreads(32, 32, 1)] void main(uint2 tid : SV_DispatchThreadID) {
	const static float logEV = -3;  // log2(0.125)

	float3 color = TexColor[tid].rgb;

	// auto exposure
	if (EnableAutoExposure) {
		float avgLuma = RWTexAdaptation.Load(0);
		color *= KeyValue / (clamp(avgLuma, AdaptationRange.x, AdaptationRange.y) + ExposureCompensation);

		// purkinje shift
		if (PurkinjeStrength > 1e-3) {
			float purkinjeMix = lerp(PurkinjeStrength, 0.f, saturate((log2(avgLuma) - logEV - PurkinjeMaxEV) / (PurkinjeStartEV - PurkinjeMaxEV)));
			if (purkinjeMix > 1e-3)
				color = PurkinjeShift(color, purkinjeMix);
		}
	} else
		color *= KeyValue / (0.18 + ExposureCompensation);

	color = Agx(color);
	color = ASC_CDL(color, AgXSlope, AgXPower, AgXOffset);
	color = Saturate(color, AgXSaturation);
	color = AgxEotf(color);

	color = saturate(color);

	RWTexOut[tid] = float4(color, 1);
}