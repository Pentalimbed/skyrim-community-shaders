#ifndef COMPUTESHADER
#	define COMPUTESHADER
#endif

#define SKY_SAMPLERS
#define PS_PREPASS
#include "PhysicalWeather/Common.hlsli"

#include "Common/Random.hlsli"

Texture2D<float> TexDepth : register(t4);

RWTexture2D<float3> RWTexTr : register(u0);
RWTexture2D<float3> RWTexLum : register(u1);

RWTexture3D<float> RWShadowVolume : register(u0);

struct RayInfo
{
	// all positions are worldspace position

	// constant
	float3 dir;
	float3 posEye;

	bool towardsSky;
	float3 posSolid;
	float distSolid;
	float3 posEnd;
	float distEnd;

	// updated
	uint marchSteps;
	float3 pos;
	float distEye;  // distance to eye

	float3 tr;
	float3 lum;
};

// return isSky
void InitRay(uint2 pxCoords, float3 rnd, out RayInfo ray)
{
	const SharedData::PhysWeatherData data = SharedData::physWeatherData;
	const float depth = TexDepth[pxCoords];
	ray.towardsSky = depth > 1 - 1e-6;

	const float2 stereoUv = (pxCoords + rnd.xy) * data.rcpFrameDim;
	const uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(stereoUv);
	const float2 uv = Stereo::ConvertFromStereoUV(stereoUv, eyeIndex);

	float4 posWorld = float4(2 * float2(uv.x, -uv.y + 1) - 1, depth, 1);
	posWorld = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], posWorld);
	posWorld.xyz = posWorld.xyz / posWorld.w;

	const float solid_dist = length(posWorld.xyz);

	ray.dir = posWorld.xyz / solid_dist;
	ray.posEye = FrameBuffer::CameraPosAdjust[eyeIndex].xyz;
	ray.distSolid = solid_dist;
	ray.posSolid = ray.posEye + posWorld.xyz;
	ray.distEnd = min(solid_dist, 16 / 1.428e-5f);
	ray.posEnd = ray.posEye + ray.dir * ray.distEnd;
	ray.marchSteps = 0;
	ray.distEye = 1e-2;
	ray.pos = ray.posEye + ray.distEye * ray.dir;
	ray.tr = 1;
	ray.lum = 0;
}

[numthreads(8, 8, 1)] void main(uint2 tid
								: SV_DispatchThreadID) {
	const SharedData::PhysWeatherData data = SharedData::physWeatherData;

	const uint2 pxCoords = tid;
	const uint3 seed = Random::pcg3d(uint3(pxCoords, pxCoords.x ^ 0xf874));
	const float3 rnd = Random::R3Modified(SharedData::FrameCountAlwaysActive, seed / 4294967295.f);

	RayInfo ray;
	InitRay(pxCoords, rnd, ray);

	// TODO temporary
	const float2 trEyeUv = TrLutUvPlanet(PosWs2Planet(ray.posEye), ray.dir);
	const float3 trEye = TexTrLut.SampleLevel(SampTr, trEyeUv, 0).rgb;
	const float2 svUv = SkyViewLutUv(ray.dir);
	if (ray.towardsSky) {
		ray.tr = trEye;
		ray.lum = TexSvLut.SampleLevel(SampSv, svUv, 0).rgb;
	} else {
		const float2 trSolidUv = TrLutUvPlanet(PosWs2Planet(ray.posSolid), ray.dir);
		const float3 trSolid = TexTrLut.SampleLevel(SampTr, trSolidUv, 0).rgb;
		ray.tr = trEye / trSolid;

		uint3 ap_dims;
		TexApLut.GetDimensions(ap_dims.x, ap_dims.y, ap_dims.z);
		const float depthSlice = lerp(.5 / ap_dims.z, 1 - .5 / ap_dims.z, saturate(ray.distSolid / AP_MAX_DIST));
		const float4 apSample = TexApLut.SampleLevel(SampSv, float3(svUv, depthSlice), 0);
		ray.lum = apSample.rgb;
	}

	RWTexTr[pxCoords] = ray.tr;
	RWTexLum[pxCoords] = ray.lum;
}