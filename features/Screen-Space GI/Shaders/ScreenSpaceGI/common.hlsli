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

- pack_half2
- unpack_half2
- flatten2D
- unflatten2D
- reconstruct_position

--------------------------

Copyright (c) Microsoft. All rights reserved.
This code is licensed under the MIT License (MIT).
THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.

Developed by Minigraph

Author:  James Stanard 

- Pack_R11G11B10_FLOAT
- Unpack_R11G11B10_FLOAT

*/

SamplerState sampler_linear_clamp : register(s0);
SamplerState sampler_point_clamp : register(s1);

cbuffer SSGICB : register(b1)
{
	float DepthRejection;

	int Range;
	float Spread;
	float RcpRangeSpreadSqr;

	uint2 BufferDim;
	float2 RcpBufferDim;

	float ZFar;
	float ZNear;
	float2 DepthUnpackConsts;
	float4x4 ViewMatrix;
	float4x4 InvViewProjMatrix;
};

uint pack_half2(in float2 value)
{
	uint retVal = 0;
	retVal = f32tof16(value.x) | (f32tof16(value.y) << 16u);
	return retVal;
}

float2 unpack_half2(in uint value)
{
	float2 retVal;
	retVal.x = f16tof32(value.x);
	retVal.y = f16tof32(value.x >> 16u);
	return retVal;
}

// 2D array index to flattened 1D array index
uint flatten2D(uint2 coord, uint2 dim)
{
	return coord.x + coord.y * dim.x;
}
// flattened array index to 2D array index
uint2 unflatten2D(uint idx, uint2 dim)
{
	return uint2(idx % dim.x, idx / dim.x);
}

// The standard 32-bit HDR color format.  Each float has a 5-bit exponent and no sign bit.
uint Pack_R11G11B10_FLOAT(float3 rgb)
{
	// Clamp upper bound so that it doesn't accidentally round up to INF
	// Exponent=15, Mantissa=1.11111
	rgb = min(rgb, asfloat(0x477C0000));
	uint r = ((f32tof16(rgb.x) + 8) >> 4) & 0x000007FF;
	uint g = ((f32tof16(rgb.y) + 8) << 7) & 0x003FF800;
	uint b = ((f32tof16(rgb.z) + 16) << 17) & 0xFFC00000;
	return r | g | b;
}

float3 Unpack_R11G11B10_FLOAT(uint rgb)
{
	float r = f16tof32((rgb << 4) & 0x7FF0);
	float g = f16tof32((rgb >> 7) & 0x7FF0);
	float b = f16tof32((rgb >> 17) & 0x7FE0);
	return float3(r, g, b);
}

// Reconstructs world-space position from depth buffer
//	uv		: screen space coordinate in [0, 1] range
//	z		: depth value at current pixel
//	InvVP	: Inverse of the View-Projection matrix that was used to generate the depth value
inline float3 reconstruct_position(in float2 uv, in float z, in float4x4 inverse_view_projection)
{
	float x = uv.x * 2 - 1;
	float y = (1 - uv.y) * 2 - 1;
	float4 position_s = float4(x, y, z, 1);
	float4 position_v = mul(inverse_view_projection, position_s);
	return position_v.xyz / position_v.w;
}
inline float3 reconstruct_position(in float2 uv, in float z)
{
	return reconstruct_position(uv, z, InvViewProjMatrix);
}

// Source: https://github.com/GPUOpen-Effects/FidelityFX-Denoiser/blob/master/ffx-shadows-dnsr/ffx_denoiser_shadows_util.h
//  LANE TO 8x8 MAPPING
//  ===================
//  00 01 08 09 10 11 18 19
//  02 03 0a 0b 12 13 1a 1b
//  04 05 0c 0d 14 15 1c 1d
//  06 07 0e 0f 16 17 1e 1f
//  20 21 28 29 30 31 38 39
//  22 23 2a 2b 32 33 3a 3b
//  24 25 2c 2d 34 35 3c 3d
//  26 27 2e 2f 36 37 3e 3f
uint bitfield_extract(uint src, uint off, uint bits)
{
	uint mask = (1u << bits) - 1;
	return (src >> off) & mask;
}  // ABfe
uint bitfield_insert(uint src, uint ins, uint bits)
{
	uint mask = (1u << bits) - 1;
	return (ins & mask) | (src & (~mask));
}  // ABfiM
uint2 remap_lane_8x8(uint lane)
{
	return uint2(bitfield_insert(bitfield_extract(lane, 2u, 3u), lane, 1u), bitfield_insert(bitfield_extract(lane, 3u, 3u), bitfield_extract(lane, 1u, 2u), 2u));
}

// from xegtao
float compute_lineardepth(float ndc)
{
	float depthLinearizeMul = DepthUnpackConsts.x;
	float depthLinearizeAdd = DepthUnpackConsts.y;
	// Optimised version of "-cameraClipNear / (cameraClipFar - projDepth * (cameraClipFar - cameraClipNear)) * cameraClipFar"
	return depthLinearizeMul / (depthLinearizeAdd - ndc);
}