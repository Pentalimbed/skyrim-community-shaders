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

#include "common.hlsli"

Texture2D<float> texture_depth : register(t0);
Texture2D<float4> texture_normal : register(t1);
Texture2D<float3> texture_input : register(t2);
Texture2D<float4> texture_velocity : register(t3);

#ifdef ATLAS
RWTexture2DArray<float> atlas2x_depth : register(u0);
RWTexture2DArray<float> atlas4x_depth : register(u1);
RWTexture2DArray<float> atlas8x_depth : register(u2);
RWTexture2DArray<float> atlas16x_depth : register(u3);
RWTexture2DArray<float3> atlas2x_color : register(u4);
RWTexture2DArray<float3> atlas4x_color : register(u5);
RWTexture2DArray<float3> atlas8x_color : register(u6);
RWTexture2DArray<float3> atlas16x_color : register(u7);
#else
RWTexture2D<float> regular2x_depth : register(u0);
RWTexture2D<float> regular4x_depth : register(u1);
RWTexture2D<float> regular8x_depth : register(u2);
RWTexture2D<float> regular16x_depth : register(u3);
RWTexture2D<float2> regular2x_normal : register(u4);
RWTexture2D<float2> regular4x_normal : register(u5);
RWTexture2D<float2> regular8x_normal : register(u6);
RWTexture2D<float2> regular16x_normal : register(u7);
#endif

groupshared float shared_depths[256];
#ifdef ATLAS
groupshared uint shared_colors[256];
#else
groupshared uint shared_normals[256];
#endif

[numthreads(8, 8, 1)] void main(uint3 Gid : SV_GroupID, uint groupIndex : SV_GroupIndex, uint3 GTid : SV_GroupThreadID, uint3 DTid : SV_DispatchThreadID) {
	uint2 dim;
	texture_depth.GetDimensions(dim.x, dim.y);
	const float2 dim_rcp = rcp(dim);

	uint2 startST = Gid.xy << 4 | GTid.xy;
	uint destIdx = GTid.y << 4 | GTid.x;
	shared_depths[destIdx + 0] = texture_depth[min(startST | uint2(0, 0), dim - 1)];
	shared_depths[destIdx + 8] = texture_depth[min(startST | uint2(8, 0), dim - 1)];
	shared_depths[destIdx + 128] = texture_depth[min(startST | uint2(0, 8), dim - 1)];
	shared_depths[destIdx + 136] = texture_depth[min(startST | uint2(8, 8), dim - 1)];

#ifdef ATLAS
	const float2 uv0 = float2(startST | uint2(0, 0)) * dim_rcp;
	const float2 uv1 = float2(startST | uint2(8, 0)) * dim_rcp;
	const float2 uv2 = float2(startST | uint2(0, 8)) * dim_rcp;
	const float2 uv3 = float2(startST | uint2(8, 8)) * dim_rcp;
	const float2 velocity0 = texture_velocity[min(startST | uint2(0, 0), dim - 1)].xy;
	const float2 velocity1 = texture_velocity[min(startST | uint2(8, 0), dim - 1)].xy;
	const float2 velocity2 = texture_velocity[min(startST | uint2(0, 8), dim - 1)].xy;
	const float2 velocity3 = texture_velocity[min(startST | uint2(8, 8), dim - 1)].xy;
	const float2 prevUV0 = uv0 + velocity0;
	const float2 prevUV1 = uv1 + velocity1;
	const float2 prevUV2 = uv2 + velocity2;
	const float2 prevUV3 = uv3 + velocity3;
	shared_colors[destIdx + 0] = Pack_R11G11B10_FLOAT(texture_input.SampleLevel(sampler_linear_clamp, prevUV0, 0));
	shared_colors[destIdx + 8] = Pack_R11G11B10_FLOAT(texture_input.SampleLevel(sampler_linear_clamp, prevUV1, 0));
	shared_colors[destIdx + 128] = Pack_R11G11B10_FLOAT(texture_input.SampleLevel(sampler_linear_clamp, prevUV2, 0));
	shared_colors[destIdx + 136] = Pack_R11G11B10_FLOAT(texture_input.SampleLevel(sampler_linear_clamp, prevUV3, 0));
#else
	shared_normals[destIdx + 0] = pack_half2(texture_normal[min(startST | uint2(0, 0), dim - 1)].xy);
	shared_normals[destIdx + 8] = pack_half2(texture_normal[min(startST | uint2(8, 0), dim - 1)].xy);
	shared_normals[destIdx + 128] = pack_half2(texture_normal[min(startST | uint2(0, 8), dim - 1)].xy);
	shared_normals[destIdx + 136] = pack_half2(texture_normal[min(startST | uint2(8, 8), dim - 1)].xy);
#endif

	GroupMemoryBarrierWithGroupSync();

	uint ldsIndex = (GTid.x << 1) | (GTid.y << 5);

	float depth = shared_depths[ldsIndex];

	uint2 st = DTid.xy;
	uint slice = flatten2D(st % 4, 4);

#ifdef ATLAS
	float3 color = Unpack_R11G11B10_FLOAT(shared_colors[ldsIndex]);

	color = color - 0.2;  // cut out pixels that shouldn't act as lights
	color *= 0.9;         // accumulation energy loss
	color = max(0, color);

	atlas2x_depth[uint3(st >> 2, slice)] = depth;
	atlas2x_color[uint3(st >> 2, slice)] = color;
#else
	float2 normal = unpack_half2(shared_normals[ldsIndex]);

	regular2x_depth[st] = depth;
	regular2x_normal[st] = normal;
#endif

	if (all(GTid.xy % 2) == 0) {
		st = DTid.xy >> 1;
		slice = flatten2D(st % 4, 4);
#ifdef ATLAS
		atlas4x_depth[uint3(st >> 2, slice)] = depth;
		atlas4x_color[uint3(st >> 2, slice)] = color;
#else
		regular4x_depth[st] = depth;
		regular4x_normal[st] = normal;
#endif

		if (all(GTid.xy % 4) == 0) {
			st = DTid.xy >> 2;
			slice = flatten2D(st % 4, 4);
#ifdef ATLAS
			atlas8x_depth[uint3(st >> 2, slice)] = depth;
			atlas8x_color[uint3(st >> 2, slice)] = color;
#else
			regular8x_depth[st] = depth;
			regular8x_normal[st] = normal;
#endif

			if (groupIndex == 0) {
				st = DTid.xy >> 3;
				slice = flatten2D(st % 4, 4);
#ifdef ATLAS
				atlas16x_depth[uint3(st >> 2, slice)] = depth;
				atlas16x_color[uint3(st >> 2, slice)] = color;
#else
				regular16x_depth[st] = depth;
				regular16x_normal[st] = normal;
#endif
			}
		}
	}
}