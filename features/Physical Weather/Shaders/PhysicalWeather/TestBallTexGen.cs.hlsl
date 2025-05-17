#ifndef COMPUTESHADER
#	define COMPUTESHADER
#endif

#define PS_PREPASS
#include "PhysicalWeather/Common.hlsli"

RWTexture3D<float3> RWTexProfile : register(u0);
RWTexture3D<float> RWTexSDF : register(u1);

const static float rBall = 0.4 / 1.428e-5f; // 200 m
const static float dimProfileDepth = 0.1 / 1.428e-5f; // 100 m

float3 TestBallCentre(){
	return CLOUD_RANGE * float3(0.5, 0.5, 0.15);
}

float TestBallSdfSampler(float3 posWorld){
	float3 centre = TestBallCentre();
	float3 posRelative = posWorld - centre;
	float sdf = length(posRelative) - rBall;

	return sdf;
}

float3 TestBallSampler(float3 posWorld){
	float3 centre = TestBallCentre();
	float3 posRelative = posWorld - centre;
	float sdf = length(posRelative) - rBall;

	float dimProfile = saturate(-sdf/ dimProfileDepth);
	float detailType = posRelative.z / rBall * 0.5 + 0.5;
	float densityScale = 1.f;

	return float3(dimProfile, detailType, densityScale);
}

[numthreads(8, 8, 1)] void main(uint3 tid
								: SV_DispatchThreadID)
{
    float3 posWorld = (tid + 0.5) / CLOUD_DIM * CLOUD_RANGE;
    float sdf = TestBallSdfSampler(posWorld);
    float3 profile = TestBallSampler(posWorld);
    RWTexProfile[tid] = profile;
    RWTexSDF[tid] = sdf;
}