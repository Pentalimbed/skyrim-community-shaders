/*
	References:
	Nubis Cubed, https://www.guerrilla-games.com/read/nubis-cubed
*/

#ifndef COMPUTESHADER
#	define COMPUTESHADER
#endif

#define SKY_SAMPLERS
#define PS_PREPASS
#include "PhysicalWeather/Common.hlsli"

#include "Common/Random.hlsli"

Texture2D<float> TexDepth : register(t4);

RWTexture2D<float4> RWTexTr : register(u0);
RWTexture2D<float4> RWTexLum : register(u1);

RWTexture3D<float> RWShadowVolume : register(u0);

const static float EPS = 1e-8;
const static float SUN_SAMPLE_STRIDE = 0.05 / 1.428e-5f; // 50 m

struct RayInfo
{
	// all positions are worldspace position

	// constant
	float3 dir;
	float3 posStart;

	bool hitSky;
	float3 posSolid;
	float distSolid;
	float3 posEnd;
	float distEnd;

	// updated
	uint marchSteps;
	float3 pos;
	float dist;  // distance to posStart

	float3 tr;
	float3 lum;
};

const static float rBall = 0.4 / 1.428e-5f; // 200 m
const static float dimProfileDepth = 0.1 / 1.428e-5f; // 100 m

float3 TestBallCentre(){
	const SharedData::PhysWeatherData data = SharedData::physWeatherData;
	return float3(data.centre, data.zBottom + CLOUD_RANGE.z * 0.15);
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

float TestBallShadowSampler(float3 posWorld){
	const SharedData::PhysWeatherData data = SharedData::physWeatherData;

	float3 centre = TestBallCentre();
	if(RayIntersectSphere(posWorld, data.lightDir, centre, rBall) < 0)
		return 0;
	float3 posRelative = posWorld - centre;
	float d = length(posRelative);
	float dProj = abs(dot(posRelative, data.lightDir));
	float dLine = sqrt(d * d - dProj * dProj);
	if(dLine >= rBall)
		return 0;
	float lIntersect = sqrt(rBall * rBall - dLine * dLine) * 2;
	return lIntersect;
}

void InitRay(uint2 pxCoords, float3 rnd, out RayInfo ray)
{
	const SharedData::PhysWeatherData data = SharedData::physWeatherData;
	const float depth = TexDepth[pxCoords];
	ray.hitSky = depth > 1 - 1e-6;

	const float2 stereoUv = (pxCoords + rnd.xy) * data.rcpFrameDim;
	const uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(stereoUv);
	const float2 uv = Stereo::ConvertFromStereoUV(stereoUv, eyeIndex);

	float4 posWorld = float4(2 * float2(uv.x, -uv.y + 1) - 1, depth, 1);
	posWorld = mul(FrameBuffer::CameraViewProjInverse[eyeIndex], posWorld);
	posWorld.xyz = posWorld.xyz / posWorld.w;

	const float solid_dist = length(posWorld.xyz);

	ray.dir = posWorld.xyz / solid_dist;
	ray.posStart = FrameBuffer::CameraPosAdjust[eyeIndex].xyz;
	ray.distSolid = solid_dist;
	ray.posSolid = ray.posStart + posWorld.xyz;
	ray.distEnd = min(solid_dist, 16 / 1.428e-5f);
	ray.posEnd = ray.posStart + ray.dir * ray.distEnd;
	ray.marchSteps = 0;
	ray.dist = 1e-2;
	ray.pos = ray.posStart + ray.dist * ray.dir;
	ray.tr = 1;
	ray.lum = 0;
}

[numthreads(8, 8, 1)] void main(uint2 tid
								: SV_DispatchThreadID) {
	const SharedData::PhysWeatherData data = SharedData::physWeatherData;

	const uint2 pxCoords = tid;
	const uint3 seed = Random::pcg3d(uint3(pxCoords, SharedData::FrameCountAlwaysActive));
	const float3 rnd = Random::R3Modified(SharedData::FrameCountAlwaysActive, seed / 4294967295.f);

	RayInfo ray;
	InitRay(pxCoords, rnd, ray);
	
	const float u = dot(ray.dir, data.lightDir);
	const float phaseAerosol = Phase::CornetteShanks(u, data.aerosolPhaseG);
	const float phaseRayleigh = Phase::Rayleigh(u);
	const float phaseCloud = lerp(Phase::ThomasSchander(u), Phase::HG(u, -0.3), 0.3);
	const float phaseCloudSecondary = Phase::HGDualLobe(u, 0.21, -0.15, 0.3);

	while(ray.marchSteps < 180 && ray.dist < ray.distEnd){
		// march distance
		float sdf = TestBallSdfSampler(ray.pos);
		float sdfDist = max(0, sdf);
		float adaptiveStepSize = max(1.0, sqrt(0.001 / 1.428e-5f * ray.dist) * 0.8);
		float distLeft = ray.distEnd - ray.dist;
		float stepSize = min(distLeft + 1, max(sdfDist, adaptiveStepSize) * lerp(0.8, 1, rnd.z));
		
		ray.dist += stepSize;
		ray.pos = ray.posStart + ray.dist * ray.dir;
		ray.marchSteps++; 

		float3 posPlanet = PosWs2Planet(ray.pos);

		// sample volume
		float rouRayleigh, rouAerosol, rouOzone;
		SampleAtmosphere(
			max(0.f, (length(posPlanet) - data.rPlanet)),
			rouRayleigh, rouAerosol, rouOzone);

		float3 cloudSample = TestBallSampler(ray.pos);
		float rouCloud = cloudSample.x;

		// calculate lighting
		float3 muSRayleigh = rouRayleigh * data.rayleighScatter;
		float3 muSAerosol = rouAerosol * data.aerosolScatter;
		float3 muSCloud = rouCloud * data.cloudScatter;

		float3 extinction = 
			muSRayleigh + muSAerosol + rouAerosol * data.aerosolAbsorption + rouOzone * data.ozoneAbsorption +
			muSCloud + rouCloud * data.cloudAbsorption;
		float3 trSample = exp(-stepSize * extinction);

		float3 scatterFactor = (1 - trSample) / max(extinction, EPS);

		// - sun transmittance
		float2 lutUv = TrLutUvPlanet(ray.pos, data.lightDir);
		float3 trAtmos = TexTrLut.SampleLevel(SampTr, lutUv, 0).rgb;

		float sumSunRouCloud = 
			TestBallSampler(ray.pos + SUN_SAMPLE_STRIDE * data.lightDir).x + 
			TestBallSampler(ray.pos + SUN_SAMPLE_STRIDE * data.lightDir * 2).x;
		float cloudShadowSample = TestBallShadowSampler(ray.pos + SUN_SAMPLE_STRIDE * data.lightDir * 2);
		float3 trSunCloud = exp(-(sumSunRouCloud * SUN_SAMPLE_STRIDE + cloudShadowSample) * (data.cloudScatter + data.cloudAbsorption));

		float3 trSun = trAtmos * trSunCloud;

		// - attenuate
		float3 inscatter = 0;
		inscatter += (phaseRayleigh * muSRayleigh + phaseAerosol * muSAerosol + phaseCloud * muSCloud) * trSun * data.lightColor;
		
		// - multiscatter approx
		float3 msVolume = cloudSample.x;
		msVolume *= exp(-(sumSunRouCloud * SUN_SAMPLE_STRIDE + cloudShadowSample) * (data.cloudScatter + data.cloudAbsorption) * 
			Remap(u, 0.0, 0.9, 0.25, Remap(sdf, -0.128 / 1.428e-5f, 0.0, 0.05, 0.25)));
		msVolume *= trAtmos;
		inscatter += muSCloud * msVolume * data.lightColor;

		float3 scatterIntegeral = inscatter * scatterFactor;

		ray.lum += scatterIntegeral * ray.tr;
		ray.tr *= trSample;

		if(any(ray.tr < EPS)){
			ray.tr = 0;
			break;
		}
	}

	// Tr and lum beyond the ray march
	const float2 svUv = SkyViewLutUv(ray.dir);
	if (ray.hitSky) {
		ray.lum += TexSvLut.SampleLevel(SampSv, svUv, 0).rgb * data.lightColor * ray.tr;
		
		const float2 trStopUv = TrLutUvPlanet(PosWs2Planet(ray.pos), ray.dir);
		const float3 trStop = TexTrLut.SampleLevel(SampTr, trStopUv, 0).rgb;
		ray.tr *= trStop;
	} else {
		uint3 ap_dims;
		TexApLut.GetDimensions(ap_dims.x, ap_dims.y, ap_dims.z);
		const float depthSlice = lerp(.5 / ap_dims.z, 1 - .5 / ap_dims.z, saturate(ray.distSolid / AP_MAX_DIST));
		const float4 apSample = TexApLut.SampleLevel(SampSv, float3(svUv, depthSlice), 0);
		ray.lum += apSample.rgb * data.lightColor * ray.tr;
		ray.tr *= apSample.a;
	}

	RWTexTr[pxCoords] = float4(ray.tr, 1);
	RWTexLum[pxCoords] = float4(ray.lum, 1);
}