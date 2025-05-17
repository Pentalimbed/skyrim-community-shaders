#ifndef COMPUTESHADER
#	define COMPUTESHADER
#endif

#define PS_PREPASS
#include "PhysicalWeather/Common.hlsli"

RWTexture3D<float3> RWTexProfile : register(u0);
RWTexture3D<float> RWTexSDF : register(u1);

const static float rBall = 0.4 / 1.428e-5f;            // 400 m
const static float dimProfileDepth = 0.1 / 1.428e-5f;  // 100 m
const static float hBox = 0.1 / 1.428e-5f;

float3 TestBallCentre()
{
	const SharedData::PhysWeatherData data = SharedData::physWeatherData;
	return float3(data.centre, data.zBottom + CLOUD_RANGE.z * 0.2);
}

float TestBallSdfSampler(float3 posWorld)
{
	float3 centre = TestBallCentre();
	float3 posRelative = posWorld - centre;
	float sdf = length(posRelative) - rBall;

	return sdf;
}

float3 TestBallSampler(float3 posWorld)
{
	float3 centre = TestBallCentre();
	float3 posRelative = posWorld - centre;
	float sdf = length(posRelative) - rBall;

	float dimProfile = saturate(-sdf / dimProfileDepth);
	float detailType = posRelative.z / rBall * 0.5 + 0.5;
	float densityScale = 1.f;

	return float3(dimProfile, detailType, densityScale);
}

float TestBoxSdfSampler(float3 posWorld)
{
	return posWorld.z - SharedData::physWeatherData.zBottom - hBox;
}

float3 TestBoxSampler(float3 posWorld)
{
	float dimProfile = (posWorld.z - SharedData::physWeatherData.zBottom > hBox) ? 0 : 1;
	float detailType = 0.f;
	float densityScale = 1.f;
	return float3(dimProfile, detailType, densityScale);
}

[numthreads(8, 8, 1)] void main(uint3 tid
								: SV_DispatchThreadID) {
	const SharedData::PhysWeatherData data = SharedData::physWeatherData;
	float3 posWorld = CloudUvw2PosWs((tid + 0.5) / CLOUD_DIM);

	float sdf = TestBallSdfSampler(posWorld);
	float3 profile = TestBallSampler(posWorld);
	// float sdf = TestBoxSdfSampler(posWorld);
	// float3 profile = TestBoxSampler(posWorld);

	RWTexProfile[tid] = profile;
	RWTexSDF[tid] = sdf;
}