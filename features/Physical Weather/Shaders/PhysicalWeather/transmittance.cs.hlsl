/*
	Adapted from: https://www.shadertoy.com/view/slSXRW
	It is said to be MIT license by the author in the comment section, but the license text is nowhere to be found.
*/

#include "common.hlsli"

RWTexture2D<float4> tex_transmittance : register(u0);

StructuredBuffer<PhysWeatherSB> phys_weather : register(t0);

float3 getSunTransmittance(float3 pos, float3 sun_dir)
{
	const uint nsteps = phys_weather[0].transmittance_step;

	// ground occlusion
	if (rayIntersectSphere(pos, sun_dir, phys_weather[0].ground_radius) > 0.0)
		return 0;

	float atmos_dist = rayIntersectSphere(pos, sun_dir, phys_weather[0].ground_radius + phys_weather[0].atmos_thickness);

	float t = 0.0;
	float3 transmittance = 1;
	for (uint i = 1; i <= nsteps; ++i) {
		float new_t = float(i) / nsteps * atmos_dist;
		float dt = new_t - t;
		t = new_t;
		float3 new_pos = pos + t * sun_dir;

		float3 rayleigh_scatter, mie_scatter, extinction;
		scatterValues(new_pos, phys_weather[0], rayleigh_scatter, mie_scatter, extinction);

		transmittance *= exp(-dt * extinction);
	}
	return transmittance;
}

[numthreads(32, 32, 1)] void main(uint3 tid
								  : SV_DispatchThreadID) {
	uint2 out_dims;
	tex_transmittance.GetDimensions(out_dims.x, out_dims.y);
	float2 uv = (tid.xy + 0.5) / out_dims;

	float cos_zenith = 2.0 * uv.x - 1.0;
	float height = phys_weather[0].ground_radius + phys_weather[0].atmos_thickness * uv.y;

	float3 pos = float3(0, 0, height);
	float3 sun_dir = normalize(float3(0, sqrt(1 - cos_zenith * cos_zenith), cos_zenith));

	tex_transmittance[tid.xy] = float4(getSunTransmittance(pos, sun_dir), 1.0);
}