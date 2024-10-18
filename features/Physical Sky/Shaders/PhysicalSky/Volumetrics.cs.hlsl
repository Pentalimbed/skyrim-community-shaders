#define PHYS_VOLS
#define SKY_SAMPLERS
#include "PhysicalSky/PhysicalSky.hlsli"

#include "Common/Random.hlsli"
#include "Common/SharedData.hlsli"
#include "Common/VR.hlsli"

Texture2D<float> TexDepth : register(t4);

Texture2DArray<float4> TexDirectShadows : register(t5);
struct PerShadow
{
	float4 VPOSOffset;
	float4 ShadowSampleParam;    // fPoissonRadiusScale / iShadowMapResolution in z and w
	float4 EndSplitDistances;    // cascade end distances int xyz, cascade count int z
	float4 StartSplitDistances;  // cascade start ditances int xyz, 4 int z
	float4 FocusShadowFadeParam;
	float4 DebugColor;
	float4 PropertyColor;
	float4 AlphaTestRef;
	float4 ShadowLightParam;  // Falloff in x, ShadowDistance squared in z
	float4x3 FocusShadowMapProj[4];
	// Since PerGeometry is passed between c++ and hlsl, can't have different defines due to strong typing
	float4x3 ShadowMapProj[2][3];
	float4x4 CameraViewProjInverse[2];
};
StructuredBuffer<PerShadow> SharedPerShadow : register(t6);

#define TERRAIN_SHADOW_REGISTER t7
#include "TerrainShadows/TerrainShadows.hlsli"

RWTexture2D<float3> RWTexTr : register(u0);
RWTexture2D<float3> RWTexLum : register(u1);

// RWTexture2D<float3> RWBeerShadowMap : register(u0);

// assume inputs are correct
void snapMarch(
	float bottom, float ceil,
	inout float3 start_pos, inout float3 end_pos, inout float3 ray_dir, inout float dist)
{
	float end_dist = clamp(dist, 0, ((ray_dir.z > 0 ? ceil : bottom) - start_pos.z) / ray_dir.z);
	end_pos = start_pos + ray_dir * end_dist;
	float start_dist = clamp(((ray_dir.z > 0 ? bottom : ceil) - start_pos.z) / ray_dir.z, 0, end_dist);
	start_pos = start_pos + ray_dir * start_dist;
	dist = end_dist - start_dist;
}

// sample sun transmittance / shadowing
float3 sampleSunTransmittance(float3 pos, float3 sun_dir, uint eyeIndex, PhySkyBufferContent info)
{
	float3 shadow = 1.0;

	float3 pos_world = convertKmPosition(pos);
	float3 pos_world_relative = pos_world - CameraPosAdjust[eyeIndex].xyz;
	float3 pos_planet = convertGamePosition(pos_world_relative) + float3(0, 0, info.planet_radius);

	// earth shadowing
	[branch] if (rayIntersectSphere(pos_planet, sun_dir, info.planet_radius) > 0.0) return 0;

	// dir shadow map
	{
		PerShadow sD = SharedPerShadow[0];
		float4 pos_camera_shifted = mul(CameraViewProj[eyeIndex], float4(pos_world_relative, 1));
		float shadow_depth = pos_camera_shifted.z / pos_camera_shifted.w;
		[branch] if (sD.EndSplitDistances.z >= shadow_depth)
		{
			uint cascade_index = sD.EndSplitDistances.x < shadow_depth;
			float3 positionLS = mul(transpose(sD.ShadowMapProj[eyeIndex][cascade_index]), float4(pos_world_relative, 1));
			float4 depths = TexDirectShadows.GatherRed(TransmittanceSampler, float3(saturate(positionLS.xy), cascade_index), 0);
			shadow *= dot(depths > positionLS.z, 0.25);
		}
	}
	[branch] if (all(shadow < 1e-8)) return 0;

	// terrain shadow
	shadow *= TerrainShadows::GetTerrainShadow(pos_world, TransmittanceSampler);
	[branch] if (all(shadow < 1e-8)) return 0;

	// atmosphere
	{
		float2 lut_uv = getHeightZenithLutUv(pos.z + info.planet_radius, sun_dir);
		shadow *= TexTransmittance.SampleLevel(TransmittanceSampler, lut_uv, 0).rgb;
	}
	[branch] if (all(shadow < 1e-8)) return 0;

	// analytic fog
	{
		float3 sun_fog_ceil_pos = pos + sun_dir * clamp((info.fog_h_max_km - pos.z) / sun_dir.z, 0, 10);
		shadow *= analyticFogTransmittance(pos, sun_fog_ceil_pos);
	}

	return shadow;
}

[numthreads(8, 8, 1)] void main(uint2 tid
								: SV_DispatchThreadID) {
	const PhySkyBufferContent info = PhysSkyBuffer[0];
	const float start_stride = 0.003;  // in km
	const float far_stride = 0.06;
	const float far_stride_dist = 1.6384;

	const uint3 seed = Random::pcg3d(uint3(tid.xy, tid.x ^ 0xf874));
	const float3 rnd = Random::R3Modified(FrameCountAlwaysActive, seed / 4294967295.f);

	///////////// get start and end
	const float depth = TexDepth[tid.xy];

	const float2 stereo_uv = (tid + rnd.xy) * info.rcp_frame_dim;
	const uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(stereo_uv);
	const float2 uv = Stereo::ConvertFromStereoUV(stereo_uv, eyeIndex);

	float4 pos_world = float4(2 * float2(uv.x, -uv.y + 1) - 1, depth, 1);
	pos_world = mul(CameraViewProjInverse[eyeIndex], pos_world);
	pos_world.xyz = pos_world.xyz / pos_world.w;

	const float ceil = info.fog_h_max_km;
	const float bottom = 0;

	float3 start_pos = convertGamePosition(CameraPosAdjust[eyeIndex].xyz);
	float3 end_pos = convertGamePosition(pos_world.xyz + CameraPosAdjust[eyeIndex].xyz);
	float3 ray_dir = end_pos - start_pos;
	float dist = length(end_pos - start_pos);
	ray_dir = ray_dir / dist;

	snapMarch(bottom, ceil, start_pos, end_pos, ray_dir, dist);

	///////////// precalc
	const float3 sun_dir = info.dirlight_dir;

	const float cos_theta = dot(ray_dir, sun_dir);
	const float3 fog_phase = miePhaseCloudMultiscatter(cos_theta);

	///////////// ray march
	float3 transmittance = 1.0;
	float3 lum = 0.0;

	float mean_depth = 0.0;
	float tr_sum = 0.0;

	float mean_shadowing = 1.0;

	const static uint max_step = 100;
	uint step = 0;
	float t = start_stride;
	float last_t = 0;
	float last_t_true = 0;
	float t_true = lerp(last_t, t, rnd.z);
	[loop] for (step = 0; step < max_step && t_true < dist; step++)
	{
		const float dt = t_true - last_t_true;
		const float3 curr_pos = start_pos + ray_dir * t_true;

		// sample scatter & extinction coeffs
		float3 fog_scatter, fog_extinction;
		sampleExponentialFog(curr_pos.z, fog_scatter, fog_extinction);

		const float3 extinction = fog_extinction;

		[branch] if (max(extinction.x, max(extinction.y, extinction.z)) > 1e-8)
		{
			// scattering
			float3 in_scatter = fog_scatter * fog_phase;
			float3 sun_transmittance = sampleSunTransmittance(curr_pos, sun_dir, eyeIndex, info);
			mean_shadowing += sun_transmittance;
			in_scatter *= sun_transmittance;

			const float3 sample_transmittance = exp(-dt * extinction);
			const float3 scatter_factor = (1 - sample_transmittance) / max(extinction, 1e-8);
			const float3 scatter_integeral = in_scatter * scatter_factor;

			// update
			lum += scatter_integeral * transmittance;
			transmittance *= sample_transmittance;
		}

		const float tr = min(transmittance.x, min(transmittance.y, transmittance.z));
		if (tr < 1 - 1e-8) {
			tr_sum += tr;
			mean_depth += t_true * tr;
		}
		[branch] if (tr < 1e-3)
		{
			step++;
			break;
		}

		// next step
		last_t_true = t_true;
		last_t = t;
		t += lerp(start_stride, far_stride, min(t / far_stride_dist, 1));
		t_true = lerp(last_t, t, rnd.z);
	}

	// ap
	mean_depth = mean_depth / max(1e-8, tr_sum);
	mean_shadowing /= float(max(step, 1));

	float linear_depth = length(pos_world.xyz);
	bool is_sky = linear_depth > 3e5;

	uint3 ap_dims;
	TexAerialPerspective.GetDimensions(ap_dims.x, ap_dims.y, ap_dims.z);
	float2 ap_uv = cylinderMapAdjusted(ray_dir);
	const float depth_slice = lerp(.5 / ap_dims.z, 1 - .5 / ap_dims.z, saturate(convertGameUnit(linear_depth) / info.aerial_perspective_max_dist));
	const float4 ap_sample = TexAerialPerspective.SampleLevel(SkyViewSampler, float3(ap_uv, depth_slice), 0);
	const float vol_depth_slice = lerp(.5 / ap_dims.z, 1 - .5 / ap_dims.z, saturate(mean_depth / info.aerial_perspective_max_dist));
	const float4 vol_ap_sample = TexAerialPerspective.SampleLevel(SkyViewSampler, float3(ap_uv, vol_depth_slice), 0);

	if (!is_sky) {
		lum = lum + (ap_sample.rgb - vol_ap_sample.rgb) * transmittance;
		transmittance *= ap_sample.a;
	}
	lum = (lum * vol_ap_sample.a + vol_ap_sample.rgb * mean_shadowing) * info.dirlight_color;

	RWTexTr[tid] = transmittance;
	RWTexLum[tid] = lum;
};

// [numthreads(8, 8, 1)] void renderBeerShadowMap(uint2 tid
// 											   : SV_DispatchThreadID) {
// 	const uint2 dims;
// 	RWBeerShadowMap.GetDimensions(dims.x, dims.y);
// 	const float2 rcp_dims = rcp(dims);
// 	const float2 uv = (tid + 0.5) * rcp_dims;

// 	const float3 target = convertGamePosition(CameraPosAdjust[eyeIndex].xyz);
// 	const float3 eye = target + sun_dir * 100;
// 	const float3 up = abs(sun_dir.z) == 1 ? float3(1, 0, 0) : float3(0, 0, 1);

// 	float3 start_pos, ray_dir;
// 	orthographicRay(uv, eye, target, up, start_pos, ray_dir);
// 	float3 dist = 11;
// 	float3 end_pos = start_pos + ray_dir * dist;
// 	snapMarch(start_pos, end_pos, ray_dir, dist);

// 	float transmittance = 1.0;
// }