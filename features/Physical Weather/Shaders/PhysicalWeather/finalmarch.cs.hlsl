/*
	define SKY_VIEW for sky_view, otherwise render as aerial perspective
	don't use them both

	Adapted from: https://www.shadertoy.com/view/slSXRW
	It is said to be MIT license by the author in the comment section, but the license text is nowhere to be found.
*/

#include "common.hlsli"

#ifdef SKY_VIEW
RWTexture2D<float4> tex_skyview : register(u0);
#else  // aerial perspective
RWTexture3D<float4> tex_aerial_perspective : register(u0);
#endif

cbuffer PerGeometry : register(b0)
{
	float3 EyePosition : packoffset(c12);
};

StructuredBuffer<PhysWeatherSB> phys_weather : register(t0);
Texture2D<float4> tex_transmittance : register(t1);
Texture2D<float4> tex_multiscatter : register(t2);

float3 raymarchScatter(float3 pos, float3 ray_dir, float3 sun_dir, float t_max, uint nsteps, uint2 tid)
{
	const float3 light_color = phys_weather[0].dirlight_color;

	float cos_theta = dot(ray_dir, sun_dir);

	float mie_phase = miePhase(cos_theta, phys_weather[0].mie_asymmetry, phys_weather[0].mie_phase_func);
	float rayleigh_phase = rayleighPhase(-cos_theta);

	float3 lum = 0, transmittance = 1;
	float t = 0;

#ifndef SKY_VIEW
	tex_aerial_perspective[uint3(tid.xy, 0)] = float4(0, 0, 0, 1);
#endif

	for (uint i = 0; i < nsteps; ++i) {
#ifdef SKY_VIEW
		float new_t = float(i) / nsteps * t_max;
#else
		float new_t = float(i + 1) / nsteps * phys_weather[0].aerial_perspective_max_dist * 1e-3;
		if (new_t <= t_max) {
#endif
		float dt = new_t - t;
		t = new_t;
		float3 new_pos = pos + t * ray_dir;

		float3 rayleigh_scatter, mie_scatter, extinction;
		scatterValues(new_pos, phys_weather[0], rayleigh_scatter, mie_scatter, extinction);

		float3 sample_transmittance = exp(-dt * extinction);

		float2 samp_coord = getLutUv(new_pos, sun_dir, phys_weather[0].ground_radius, phys_weather[0].atmos_thickness);
		float3 sun_transmittance = tex_transmittance.SampleLevel(MirrorLinearSampler, samp_coord, 0).rgb;
		float3 psi_ms = tex_multiscatter.SampleLevel(MirrorLinearSampler, samp_coord, 0).rgb;

		float3 rayleigh_inscatter = rayleigh_scatter * (rayleigh_phase * sun_transmittance + psi_ms);
		float3 mie_inscatter = mie_scatter * (mie_phase * sun_transmittance + psi_ms);
		float3 in_scatter = rayleigh_inscatter + mie_inscatter;

		float3 scatter_integeral = in_scatter * (1 - sample_transmittance) / extinction;

		lum += scatter_integeral * transmittance;
		transmittance *= sample_transmittance;
#ifndef SKY_VIEW
	}
	tex_aerial_perspective[uint3(tid.xy, i + 1)] = float4(lum * light_color, rgbLuminance(transmittance));
#endif
}
return lum;
}

[numthreads(32, 32, 1)] void main(uint3 tid
								  : SV_DispatchThreadID) {
	uint3 out_dims;
#ifdef SKY_VIEW
	tex_skyview.GetDimensions(out_dims.x, out_dims.y);
#else
		tex_aerial_perspective.GetDimensions(out_dims.x, out_dims.y, out_dims.z);
#endif
	float2 uv = (tid.xy + 0.5) / out_dims.xy;

	float3 ray_dir = invCylinderMapAdjusted(uv);
	// float3 ray_dir = invLambAzAdjusted(uv, 0);

	float height = (EyePosition.z - phys_weather[0].bottom_z) * phys_weather[0].unit_scale.y * 1.428e-8 + phys_weather[0].ground_radius;
	float3 view_pos = float3(0, 0, height);
	float ground_dist = rayIntersectSphere(view_pos, ray_dir, phys_weather[0].ground_radius);
	float atmos_dist = rayIntersectSphere(view_pos, ray_dir, phys_weather[0].ground_radius + phys_weather[0].atmos_thickness);
	float t_max = ground_dist > 0.0 ? ground_dist : atmos_dist;

	float3 lum = raymarchScatter(view_pos, ray_dir, phys_weather[0].dirlight_dir, t_max,
#ifdef SKY_VIEW
		phys_weather[0].skyview_step,
#else
			out_dims.z - 1,
#endif
		tid.xy);

#ifdef SKY_VIEW
	tex_skyview[tid.xy] = float4(lum * phys_weather[0].dirlight_color, 1);
#endif
}
