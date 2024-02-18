/// By ProfJack/五脚猫, 2024-2-17 UTC

#include "common.hlsli"

RWTexture2D<float4> RWTexOut : register(u0);

Texture2D<float4> TexColor : register(t0);
Texture1D<float> RWTexAdaptation : register(t1);

cbuffer TonemapCB : register(b0)
{
	uint EnableAutoExposure;
	float Exposure;

	float AgXSlope;
	float AgXPower;
	float AgXOffset;
	float AgXSaturation;
};

SamplerState SampColor
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Mirror;
	AddressV = Mirror;
	AddressW = Mirror;
};

[numthreads(32, 32, 1)] void main(uint2 tid
								  : SV_DispatchThreadID) {
	float3 color = TexColor[tid].rgb;

	// auto exposure
	if (EnableAutoExposure) {
		float avgLuma = RWTexAdaptation.Load(0);
		color *= Exposure / (9.6 * avgLuma);
	} else
		color *= Exposure;

	color = Agx(color);
	color = ASC_CDL(color, AgXSlope, AgXPower, AgXOffset);
	color = Saturate(color, AgXSaturation);
	color = AgxEotf(color);

	color = saturate(color);

	RWTexOut[tid] = float4(color, 1);
}