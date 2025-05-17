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

RWTexture2D<float4> RWTexTr : register(u0);
RWTexture2D<float4> RWTexLum : register(u1);

RWTexture3D<float> RWCloudShadow : register(u0);

const static float EPS = 1e-8;
const static uint MAX_STEP = 150;
const static float SUN_SAMPLE_STRIDE = 0.1 / 1.428e-5f; // 100 m

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

void sampleCloudDensity(
	float3 posWorld, float eye_dist, float mip_level, bool upres,
	out float3 profile, out float density)
{
	const SharedData::PhysWeatherData data = SharedData::physWeatherData;
	
	density = 0;
	profile = TexCloudProfile.SampleLevel(SampTr, PosWs2CloudUvw(posWorld), 0);
	if (profile.x < 1e-8)
		return;
	
	// sample noise
	// float3 offset = data.cloudNoiseOffset;
	float3 offset = 0;
	float4 noise = TexNoise.SampleLevel(SampNoise, (posWorld + offset) * data.cloudNoiseFreq * 1, mip_level);
	// Define wispy noise
	float wispy_noise = lerp(noise.r, noise.g, profile.x);
	// Define billowy noise
	float billowy_type_gradient = pow(profile.x, 0.25);
	float billowy_noise = lerp(noise.b * 0.3, noise.a * 0.3, billowy_type_gradient);
	// Define Noise composite - blend to wispy as the density scale decreases.
	float noise_composite = lerp(wispy_noise, billowy_noise, profile.y);

	// Upres
	float hhf_fraction;
	if (upres) {
		// Get the hf noise by folding the highest frequency billowy noise.
		float hhf_noise = saturate(lerp(1.0 - pow(abs(abs(noise.g * 2.0 - 1.0) * 2.0 - 1.0), 4.0), pow(abs(abs(noise.a * 2.0 - 1.0) * 2.0 - 1.0), 2.0), profile.y));

		// Apply the HF nosie near camera.
		hhf_fraction = (eye_dist - 0.05 / 1.428e-5f) / (0.15 / 1.428e-5f - 0.05 / 1.428e-5f);
		float hhf_noise_distance_range_blender = lerp(0.9, 1.0, hhf_fraction);
		noise_composite = lerp(hhf_noise, noise_composite, hhf_noise_distance_range_blender);
	}

	density = saturate((profile.x - noise_composite) / (1 - noise_composite));
	float powered_density_scale = pow(saturate(profile.z), 4.0); 
	density *= powered_density_scale; 

	// Sharpen result
	density = pow(density, lerp(0.3, 0.6, max(EPS, powered_density_scale)));
	if (upres) 
		density = pow(density, lerp(0.5, 1.0, hhf_fraction)) * lerp(0.666, 1.0, hhf_fraction);
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define SHADOW_NTHREADS 256
groupshared float gDensity[SHADOW_NTHREADS];

[numthreads(SHADOW_NTHREADS, 1, 1)] void renderShadow(const uint gtid : SV_GroupThreadID, const uint2 gid : SV_GroupID){
	const SharedData::PhysWeatherData data = SharedData::physWeatherData;

	uint3 dims;
	RWCloudShadow.GetDimensions(dims.x, dims.y, dims.z);
	const float3 rcpDims = rcp(dims);

	const float3 rayDir = -data.lightDir;  // from sun

	float3 rayPxIncrement = rayDir / CLOUD_RANGE * dims;
	const float dirMaxComponent = max(max(abs(rayPxIncrement.x), abs(rayPxIncrement.y)), abs(rayPxIncrement.z));

	uint3 startPx;
	bool3 componentMask = false;
	if (abs(rayPxIncrement.x) == dirMaxComponent) {
		startPx = uint3(rayPxIncrement.x > 0 ? 0 : dims.x - 1, gid);
		componentMask.x = true;
	} else if (abs(rayPxIncrement.y) == dirMaxComponent) {
		startPx = uint3(gid.x, rayPxIncrement.y > 0 ? 0 : dims.y - 1, gid.y);
		componentMask.y = true;
	} else {
		startPx = uint3(gid, rayPxIncrement.z > 0 ? 0 : dims.z - 1);
		componentMask.z = true;
	}
	rayPxIncrement /= dirMaxComponent;
	const float3 rayUvIncrement = rayPxIncrement * rcpDims;
	const float3 startUv = (startPx + 0.5) * rcpDims;
	const float3 rawThreadUv = startUv + gtid * rayUvIncrement;

	const bool3 isUvInRange = (rawThreadUv > 0) && (rawThreadUv < 1);
	const bool isValid = dot(isUvInRange, componentMask);

	const float3 threadUv = rawThreadUv - floor(rawThreadUv);  // wraparound
	const uint3 threadPxCoord = threadUv * dims;

	float pastDensity = RWCloudShadow[threadPxCoord];
	if (ISNAN(pastDensity))
		pastDensity = 0;

	if (isValid) {
		const float3 pos = CloudUvw2PosWs(threadUv);

		// fetch density using only ndf
		float3 cloudSample; 
		float rou;
		sampleCloudDensity(pos, 1e8, 0, false, cloudSample, rou);

		gDensity[gtid] = rou * length(rayUvIncrement * CLOUD_RANGE);
	}
	GroupMemoryBarrierWithGroupSync();

	// parallel summation
	[unroll] for (uint offset = 1; offset < SHADOW_NTHREADS; offset <<= 1)
	{
		if (isValid && gtid >= offset) {
			if (all(floor(rawThreadUv - rayUvIncrement * offset) == floor(rawThreadUv)))  // no wraparound happened
			{
				float rouCurrent = gDensity[gtid];
				float rouSample = gDensity[gtid - offset];
				gDensity[gtid] = rouCurrent + rouSample;
			}
		}
		GroupMemoryBarrierWithGroupSync();
	}

	// save
	if (isValid) {
		RWCloudShadow[threadPxCoord] = lerp(pastDensity, gDensity[gtid], 0.1f);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

	while(ray.marchSteps < MAX_STEP && ray.dist < ray.distEnd){
		// march distance
		float sdf = TexCloudSdf.SampleLevel(SampTr, PosWs2CloudUvw(ray.pos), 0);
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

		// float3 cloudSample = TestBallSampler(ray.pos);
		// float rouCloud = cloudSample.x;
		float3 cloudSample; 
		float rouCloud;
		sampleCloudDensity(ray.pos, ray.dist, 0, ray.dist < 0.15 / 1.428e-5f, cloudSample, rouCloud);

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
		
		float rouCloud1, rouCloud2;
		float3 tmp;
		sampleCloudDensity(ray.pos + SUN_SAMPLE_STRIDE * data.lightDir, ray.dist, 0, false, tmp, rouCloud1);
		sampleCloudDensity(ray.pos + SUN_SAMPLE_STRIDE * data.lightDir * 2, ray.dist, 0, false, tmp, rouCloud2);
		float sumSunRouCloud = rouCloud1 + rouCloud2;

		float3 posCloudShadow;
		bool hasCloudShadow = SnapPosToShadowBox(ray.pos + SUN_SAMPLE_STRIDE * data.lightDir * 2, posCloudShadow);
		float cloudShadowSample = hasCloudShadow ? TexCloudShadow.SampleLevel(SampTr, PosWs2CloudUvw(posCloudShadow), 0) : 0;
		
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

	// visualize steps
	// ray.lum = ray.marchSteps / (float)MAX_STEP;

	RWTexTr[pxCoords] = float4(ray.tr, 1);
	RWTexLum[pxCoords] = float4(ray.lum, 1);
}

