/*

src: https://github.com/turanszkij/WickedEngine

Copyright (c) 2024 Turánszki János

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

#include "../Common/GBuffer.hlsl"
#include "common.hlsli"

Texture2D<float> input_depth_low : register(t0);
Texture2D<float2> input_normal_low : register(t1);
Texture2D<float3> input_diffuse_low : register(t2);
Texture2D<float> input_depth_high : register(t3);
Texture2D<float4> input_normal_high : register(t4);

RWTexture2D<float3> output : register(u0);

static const float depthThreshold = 0.1;
static const float normalThreshold = 64;
static const uint POSTPROCESS_BLOCKSIZE = 8;

groupshared float3 gs_P[POSTPROCESS_BLOCKSIZE][POSTPROCESS_BLOCKSIZE];

[numthreads(POSTPROCESS_BLOCKSIZE, POSTPROCESS_BLOCKSIZE, 1)] void main(
	uint2 Gid : SV_GroupID, uint groupIndex : SV_GroupIndex,
	uint2 DTid : SV_DispatchThreadID, uint2 GTid : SV_GroupThreadID) {
	// uint2 GTid = remap_lane_8x8(groupIndex);
	// uint2 pixel = Gid * POSTPROCESS_BLOCKSIZE + GTid;
	uint2 pixel = DTid;
	const float2 uv = (pixel + 0.5) * RcpBufferDim;

	const float depth = input_depth_high[pixel];
	const float linearDepth = compute_lineardepth(depth);
	const float3 N = DecodeNormal(input_normal_high[pixel].rg);

#if 1
	const float3 P = reconstruct_position(uv, depth);

	// const float3 ddxP = P - QuadReadAcrossX(P);
	// const float3 ddyP = P - QuadReadAcrossY(P);

	gs_P[GTid.x][GTid.y] = P;
	GroupMemoryBarrierWithGroupSync();
	const float3 ddxP = P - gs_P[GTid.x ^ 1u][GTid.y];
	GroupMemoryBarrierWithGroupSync();
	const float3 ddyP = P - gs_P[GTid.x][GTid.y ^ 1u];

	const float curve = saturate(1 - pow(1 - max(dot(ddxP, ddxP), dot(ddyP, ddyP)), 32));
	const float normalPow = lerp(NormalThreshold, 1, curve);
#else
	const float normalPow = NormalThreshold;
#endif

	float3 result = 0;
	float sum = 0;
#if 1
	const int range = Range;
	const float spread = Spread;
#else
	const int range = 2;
	const float spread = 6;
#endif
	for (int x = -range; x <= range; ++x) {
		for (int y = -range; y <= range; ++y) {
			const float2 offset = float2(x, y) * spread * RcpBufferDim;
			const float2 sample_uv = uv + offset;

			const float3 sampleDiffuse = input_diffuse_low.SampleLevel(sampler_linear_clamp, sample_uv, 0).rgb;

			const float sampleDepth = input_depth_low.SampleLevel(sampler_point_clamp, sample_uv, 0);
			const float sampleLinearDepth = compute_lineardepth(sampleDepth);
			float bilateralDepthWeight = 1 - saturate(abs(sampleLinearDepth - linearDepth) * DepthThreshold);

			const float3 sampleN = DecodeNormal(input_normal_low.SampleLevel(sampler_linear_clamp, sample_uv, 0));
			float normalError = pow(saturate(dot(sampleN, N)), normalPow);
			float bilateralNormalWeight = normalError;

			float weight = bilateralDepthWeight * bilateralNormalWeight;

			//weight = 1;
			result += sampleDiffuse * weight;
			sum += weight;
		}
	}

	if (sum > 0) {
		result /= sum;
	}

	result = max(0, result);

	output[pixel] = output[pixel] + result;
	//output[pixel] = float4(curve.xxx, 1);
}