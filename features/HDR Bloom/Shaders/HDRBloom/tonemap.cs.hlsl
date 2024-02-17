// By ProfJack/五脚猫, 2024-2-17 UTC

#include "common.hlsli"

RWTexture2D<float4> RWTexOut : register(u0);

Texture2D<float4> TexColor : register(t0);

SamplerState SampColor
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Clamp;
	AddressV = Clamp;
	AddressW = Clamp;
};

cbuffer TonemapCB : register(b0)
{
	float Exposure;

	float AgXSlope;
	float AgXPower;
	float AgXOffset;
	float AgXSaturation;
};

[numthreads(32, 32, 1)] void main(
	uint2 tid : SV_DispatchThreadID) {
	float3 color = TexColor[tid].rgb;
	color *= Exposure;

	color = Agx(color);
	color = ASC_CDL(color, AgXSlope, AgXPower, AgXOffset);
	color = Saturate(color, AgXSaturation);
	color = AgxEotf(color);

	// debug
	if (all(color < 0))
		color = float3(0, 0, 1);
	if (all(color > 1))
		color = float3(1, 0, 0);

	color = saturate(color);
	RWTexOut[tid] = float4(color, 1);
}