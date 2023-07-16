/*
	Adapted from: https://www.shadertoy.com/view/slSXRW
	It is said to be MIT license by the author in the comment section, but the license text is nowhere to be found.
*/

#include "common.hlsli"

RWTexture2D<float4> tex_multiscatter : register(u0);

StructuredBuffer<PhysWeatherSB> phys_weather : register(t0);
Texture2D<float4> tex_transmittance : register(t1);

void getMultiscatterValues(
	float3 pos, float3 sun_dir,
	out float3 lum_total, out float3 f_ms)
{
	const uint nsteps = phys_weather[0].multiscatter_step;
	const uint sqrt_samples = phys_weather[0].multiscatter_sqrt_samples;

	lum_total = 0;
	f_ms = 0;

	float rcp_samples = rcp(float(sqrt_samples * sqrt_samples));
	for (uint i = 0; i < sqrt_samples; ++i)
		for (uint j = 0; j < sqrt_samples; ++j) {
			float theta = (i + 0.5) * PI / sqrt_samples;
			float phi = acos(1.0 - 2.0 * (j + 0.5) / sqrt_samples);
			float3 ray_dir = sphericalDir(theta, phi);

			float ground_dist = rayIntersectSphere(pos, ray_dir, phys_weather[0].ground_radius);
			float atmos_dist = rayIntersectSphere(pos, ray_dir, phys_weather[0].ground_radius + phys_weather[0].atmos_thickness);
			float t_max = ground_dist > 0 ? ground_dist : atmos_dist;

			float cos_theta = dot(ray_dir, sun_dir);
			float mie_phase = miePhase(cos_theta, phys_weather[0].mie_asymmetry, phys_weather[0].mie_phase_func);
			float rayleigh_phase = rayleighPhase(-cos_theta);

			float3 lum = 0, lum_factor = 0, transmittance = 1;
			float t = 0;
			for (uint step = 1; step <= nsteps; ++step) {
				float new_t = float(step) / nsteps * t_max;
				float dt = new_t - t;
				t = new_t;
				float3 new_pos = pos + t * ray_dir;

				float3 rayleigh_scatter, mie_scatter, extinction;
				scatterValues(new_pos, phys_weather[0], rayleigh_scatter, mie_scatter, extinction);

				float3 sample_transmittance = exp(-dt * extinction);

				float3 scatter_no_phase = rayleigh_scatter + mie_scatter;
				float3 scatter_f = (scatter_no_phase - scatter_no_phase * sample_transmittance) / extinction;
				lum_factor += transmittance * scatter_f;

				float2 samp_coord = getLutUv(new_pos, sun_dir, phys_weather[0].ground_radius, phys_weather[0].atmos_thickness);
				float3 sun_transmittance = tex_transmittance.SampleLevel(MirrorLinearSampler, samp_coord, 0).rgb;

				float3 rayleigh_inscatter = rayleigh_scatter * rayleigh_phase;
				float mie_inscatter = mie_scatter * mie_phase;
				float3 in_scatter = (rayleigh_inscatter + mie_inscatter) * sun_transmittance;

				float3 scatter_integeral = (in_scatter - in_scatter * sample_transmittance) / extinction;

				lum += scatter_integeral * transmittance;
				transmittance *= sample_transmittance;
			}

			if (ground_dist > 0) {
				float3 hit_pos = pos + ground_dist * ray_dir;
				if (dot(pos, sun_dir) > 0) {
					hit_pos = normalize(hit_pos) * phys_weather[0].ground_radius;
					float2 samp_coord = getLutUv(hit_pos, sun_dir, phys_weather[0].ground_radius, phys_weather[0].atmos_thickness);
					lum += transmittance * phys_weather[0].ground_albedo * tex_transmittance.SampleLevel(MirrorLinearSampler, samp_coord, 0).rgb;
				}
			}

			f_ms += lum_factor * rcp_samples;
			lum_total += lum * rcp_samples;
		}
}

[numthreads(32, 32, 1)] void main(uint3 tid
								  : SV_DispatchThreadID) {
	uint2 out_dims;
	tex_multiscatter.GetDimensions(out_dims.x, out_dims.y);
	float2 uv = (tid.xy + 0.5) / out_dims;

	float cos_zenith = 2.0 * uv.x - 1.0;
	float height = phys_weather[0].ground_radius + phys_weather[0].atmos_thickness * uv.y;

	float3 pos = float3(0, 0, height);
	float3 sun_dir = normalize(float3(0, -sqrt(1 - cos_zenith * cos_zenith), cos_zenith));

	float3 lum, f_ms;
	getMultiscatterValues(pos, sun_dir, lum, f_ms);

	float3 psi = lum / (1 - f_ms);
	tex_multiscatter[tid.xy] = float4(psi, 1);
}
