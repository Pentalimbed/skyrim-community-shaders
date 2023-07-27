/*
	Tileable Perlin-Worley 3D
	url: https://www.shadertoy.com/view/3dVXDc

	Procedural Cloudscapes (supplementary material)
	url: http://webanck.fr/publications/Webanck2018-supplementary_material.pdf
*/
#include "common.hlsli"

SamplerState SampNoise : register(s0);

StructuredBuffer<PhysWeatherSB> phys_weather : register(t0);
Texture2D<float4> tex_transmittance : register(t1);
Texture3D<float4> tex_noise : register(t2);

RWTexture2D<float4> tex_cloud_scatter : register(u0);
RWTexture2D<float4> tex_cloud_transmittance : register(u1);

cbuffer PerFrame : register(b0)
{
	row_major float4x4 ViewMatrix : packoffset(c0);
	row_major float4x4 ProjMatrix : packoffset(c4);
	row_major float4x4 ViewProjMatrix : packoffset(c8);
	row_major float4x4 ViewProjMatrixUnjittered : packoffset(c12);
	row_major float4x4 PreviousViewProjMatrixUnjittered : packoffset(c16);
	row_major float4x4 InvProjMatrixUnjittered : packoffset(c20);
	row_major float4x4 ProjMatrixUnjittered : packoffset(c24);
	row_major float4x4 InvViewMatrix : packoffset(c28);
	row_major float4x4 InvViewProjMatrix : packoffset(c32);
	row_major float4x4 InvProjMatrix : packoffset(c36);
	float4 CurrentPosAdjust : packoffset(c40);
	float4 PreviousPosAdjust : packoffset(c41);
	// notes: FirstPersonY seems 1.0 regardless of third/first person, could be LE legacy stuff
	float4 GammaInvX_FirstPersonY_AlphaPassZ_CreationKitW : packoffset(c42);
	float4 DynamicRes_WidthX_HeightY_PreviousWidthZ_PreviousHeightW : packoffset(c43);
	float4 DynamicRes_InvWidthX_InvHeightY_WidthClampZ_HeightClampW : packoffset(c44);
}

float getCloudLayerDensity(float3 pos, CloudLayer cloud)
{
	static const float rcp_1080 = 1 / 1080.0;

	float altitude = length(pos) - phys_weather[0].ground_radius;
	float3 scaled_pos = pos * 1.428e-2;
	float density = 0;
	if ((altitude > cloud.height_range.x) && (altitude < cloud.height_range.y)) {
		float n1 = tex_noise.SampleLevel(SampNoise, scaled_pos * float3(0.35, 1, 1) * cloud.freq * 3.6, 0).r;
		float n2 = tex_noise.SampleLevel(SampNoise, scaled_pos * rcp_1080 * cloud.freq * 9.6, 0).r;
		float n3 = tex_noise.SampleLevel(SampNoise, scaled_pos * rcp_1080 * cloud.freq * 40, 0).g;

		float k = cloud.altocumulus_blend;

		float phi = lerp(smoothstep(0.43, 1, n1), 1, k);
		float delta = lerp(lerp(n2, 1 - n3, 0.25), 0, k);

		density += max(0, phi - delta);
	}

	return density;
}

#define UI0 1597334673U
#define UI1 3812015801U
#define UI2 uint2(UI0, UI1)
#define UI3 uint3(UI0, UI1, 2798796415U)
#define UIF (1.0 / float(0xffffffffU))

float3 hash33(float3 p)
{
	uint3 q = uint3(int3(p)) * UI3;
	q = (q.x ^ q.y ^ q.z) * UI3;
	return -1. + 2. * float3(q) * UIF;
}

[numthreads(32, 32, 1)] void main(uint3 tid
								  : SV_DispatchThreadID) {
	uint2 out_dims;
	tex_cloud_scatter.GetDimensions(out_dims.x, out_dims.y);
	float2 uv = (tid.xy + 0.5) / out_dims;

	float4 _view_dir = mul(InvViewProjMatrix, float4(float2(uv.x, 1 - uv.y) * 2 - 1, 0, 1));
	float3 view_dir = normalize(_view_dir.xyz / _view_dir.w);

	float height = (CurrentPosAdjust.z - phys_weather[0].bottom_z) * phys_weather[0].unit_scale * 1.428e-5 + phys_weather[0].ground_radius;
	float3 pos = float3(0, 0, height);
	float ground_dist = rayIntersectSphere(pos, view_dir, phys_weather[0].ground_radius);
	float lower_dist = rayIntersectSphere(pos, view_dir, phys_weather[0].ground_radius + phys_weather[0].cloud_layer.height_range.x);
	float higher_dist = rayIntersectSphere(pos, view_dir, phys_weather[0].ground_radius + phys_weather[0].cloud_layer.height_range.y);
	float t_begin = 0, t_end = ground_dist;
	if (ground_dist < 0) {
		t_begin = lower_dist;
		t_end = higher_dist;
	}

	float cos_theta = dot(view_dir, phys_weather[0].dirlight_dir);
	float4 cloud_phase;
	cloud_phase = miePhase(cos_theta, phys_weather[0].cloud_phase_func);
	if (phys_weather[0].multiscatter_octaves > 0)
		cloud_phase.y = miePhase(cos_theta * .5, phys_weather[0].cloud_phase_func);
	if (phys_weather[0].multiscatter_octaves > 1)
		cloud_phase.z = miePhase(cos_theta * .25, phys_weather[0].cloud_phase_func);
	if (phys_weather[0].multiscatter_octaves > 2)
		cloud_phase.w = miePhase(cos_theta * .125, phys_weather[0].cloud_phase_func);

	float3 jitter = hash33(float3(uv, phys_weather[0].timer) * 1e6);
	// float3 jitter = 0;
	float stride = (t_end - t_begin) / phys_weather[0].cloud_march_step;

	float3 lum = 0, transmittance = 1;
	for (uint i = 0; i < phys_weather[0].cloud_march_step; ++i) {
		float3 new_pos = pos + (t_begin + (i - jitter.x) * stride) * view_dir;

		float density = getCloudLayerDensity(new_pos, phys_weather[0].cloud_layer);
		float3 cloud_scatter = phys_weather[0].cloud_layer.scatter * density;
		float3 extinction = cloud_scatter + phys_weather[0].cloud_layer.absorption * density;

		float3 sample_transmittance = exp(-stride * extinction);

		if (density > 1e-6) {
			float2 samp_coord = getLutUv(new_pos, phys_weather[0].dirlight_dir, phys_weather[0].ground_radius, phys_weather[0].atmos_thickness);
			float3 sun_transmittance = tex_transmittance.SampleLevel(MirrorLinearSampler, samp_coord, 0).rgb;

			float3 sun_optical_depth = 0;
			float3 sun_pos = new_pos;
			for (uint j = 0; j < phys_weather[0].cloud_self_shadow_step; ++j) {
				float3 sun_pos = new_pos + (j - jitter.y) * stride * phys_weather[0].dirlight_dir;
				float3 sun_extinction = (phys_weather[0].cloud_layer.scatter + phys_weather[0].cloud_layer.absorption) * getCloudLayerDensity(sun_pos, phys_weather[0].cloud_layer);
				sun_optical_depth += stride * sun_extinction;
			}

			float falloff = 1;
			for (uint oct = 0; oct <= phys_weather[0].multiscatter_octaves; ++oct) {
				float octave_cloud_phase = oct < 1 ? cloud_phase.x : (oct < 2 ? cloud_phase.y : (oct < 3 ? cloud_phase.z : cloud_phase.w));

				// beer-powder
				sun_transmittance *= exp(-sun_optical_depth * falloff) * (1 - exp(-sun_optical_depth * sun_optical_depth * falloff * falloff));
				float3 in_scatter = cloud_scatter * falloff * (octave_cloud_phase.x * sun_transmittance);
				float3 scatter_integeral = in_scatter * (1 - pow(sample_transmittance, falloff)) / max(extinction * falloff, 1e-10);
				lum += scatter_integeral * pow(transmittance, falloff);

				falloff *= 0.5;
			}
		}

		transmittance *= sample_transmittance;

		if (all(transmittance < 1e-3))
			break;
	}

	tex_cloud_scatter[tid.xy] = float4(lum * phys_weather[0].dirlight_color, 1);
	tex_cloud_transmittance[tid.xy] = float4(transmittance, 1);
}
