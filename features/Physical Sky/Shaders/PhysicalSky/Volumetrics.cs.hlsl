#define PHYS_VOLS
#define SKY_SAMPLERS
#include "PhysicalSky.hlsli"

#include "../Common/FrameBuffer.hlsli"
#include "../Common/Random.hlsli"
#include "../Common/VR.hlsli"

Texture2D<float> TexDepth : register(t4);

RWTexture2D<float3> RWTexTr : register(u0);
RWTexture2D<float3> RWTexLum : register(u1);

// sample sun transmittance / shadowing
float3 sampleSunTransmittance(float3 pos, float3 sun_dir, PhySkyBufferContent info)
{
	float2 lut_uv = getHeightZenithLutUv(pos.z + info.planet_radius, sun_dir);
	float3 atmo_transmittance = TexTransmittance.SampleLevel(TransmittanceSampler, lut_uv, 0).rgb;

	return atmo_transmittance;
}

[numthreads(32, 32, 1)] void main(uint2 tid
								  : SV_DispatchThreadID) {
	const PhySkyBufferContent info = PhysSkyBuffer[0];
	const float start_stride = 0.003;  // in km
	const float far_stride = 0.06;
	const float far_dist = 1.6384;

	// get start and end
	float depth = TexDepth[tid.xy];

	float2 stereo_uv = tid * info.rcp_frame_dim;
	uint eyeIndex = Stereo::GetEyeIndexFromTexCoord(stereo_uv);
	float2 uv = Stereo::ConvertFromStereoUV(stereo_uv, eyeIndex);

	float4 pos_world = float4(2 * float2(uv.x, -uv.y + 1) - 1, depth, 1);
	pos_world = mul(CameraViewProjInverse[eyeIndex], pos_world);
	pos_world.xyz = pos_world.xyz / pos_world.w;

	float3 start_pos = convertGamePosition(CameraPosAdjust[eyeIndex].xyz);
	float3 end_pos = convertGamePosition(pos_world.xyz + CameraPosAdjust[eyeIndex].xyz);
	float3 ray_dir = end_pos - start_pos;
	float dist = length(end_pos - start_pos);
	ray_dir = ray_dir / dist;

	float ceil = info.fog_h_max_km;
	dist = clamp(dist, 0, ray_dir.z > 0 ? (ceil - start_pos.z) / ray_dir.z : -start_pos.z / ray_dir.z);
	end_pos = start_pos + ray_dir * dist;

	// precalc
	float3 sun_dir = info.dirlight_dir;

	float cos_theta = dot(ray_dir, sun_dir);
	float3 fog_phase = miePhaseCloudMultiscatter(cos_theta);

	uint2 seed = Random::pcg2d(tid.xy);
	float2 rnd = Random::R2Modified(info.frame_index, seed / 4294967295.f);

	// ray march
	float3 transmittance = 1.0;
	float3 lum = 0.0;

	float mean_depth = 0.0;
	float tr_sum = 0.0;

	const static uint max_step = 100;
	uint step = 0;
	float t = start_stride;
	float last_t = 0;
	float last_t_true = 0;
	for (step = 0; step < max_step && last_t < dist; step++) {
		float t_true = lerp(last_t, t, rnd.x);
		float dt = t_true - last_t_true;
		float3 curr_pos = start_pos + ray_dir * t_true;

		// sample scatter & extinction coeffs
		float3 fog_scatter, fog_extinction;
		sampleExponentialFog(curr_pos.z, fog_scatter, fog_extinction);

		float3 extinction = fog_extinction;

		if (max(extinction.x, min(extinction.y, extinction.z)) > 1e-8) {
			// scattering
			float3 in_scatter = fog_scatter * fog_phase;
			in_scatter *= sampleSunTransmittance(curr_pos, sun_dir, info);

			float3 sample_transmittance = exp(-dt * extinction);
			float3 scatter_factor = (1 - sample_transmittance) / max(extinction, 1e-8);
			float3 scatter_integeral = in_scatter * scatter_factor;

			// update
			lum += scatter_integeral * transmittance;
			transmittance *= sample_transmittance;
		}

		float tr = min(transmittance.x, min(transmittance.y, transmittance.z));
		tr_sum += tr;
		mean_depth += t_true * tr_sum;

		if (tr < 1e-3)
			break;

		// next step
		last_t_true = t_true;
		last_t = t;
		t += lerp(start_stride, far_stride, min(t / far_dist, 1));
	}

	mean_depth = mean_depth / max(1e-8, tr_sum);

	RWTexTr[tid] = transmittance;
	RWTexLum[tid] = lum * info.dirlight_color;
}