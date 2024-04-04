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
	float DepthThreshold;
	float NormalThreshold;

	int Range;
	float Spread;
	float RcpRangeSpreadSqr;

	uint2 BufferDim;
	float2 RcpBufferDim;

	float2 NDCToViewMul;
	float2 NDCToViewAdd;
	float2 DepthUnpackConsts;
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

float compute_lineardepth(float ndc)
{
	float depthLinearizeMul = DepthUnpackConsts.x;
	float depthLinearizeAdd = DepthUnpackConsts.y;
	// Optimised version of "-cameraClipNear / (cameraClipFar - projDepth * (cameraClipFar - cameraClipNear)) * cameraClipFar"
	return depthLinearizeMul / (depthLinearizeAdd - ndc);
}

// Inputs are screen XY and viewspace depth, output is viewspace position
float3 ScreenSpaceToViewSpacePosition(const float2 screenPos, const float viewspaceDepth)
{
	float3 ret;
	ret.xy = (NDCToViewMul * screenPos.xy + NDCToViewAdd) * viewspaceDepth;
	ret.z = viewspaceDepth;
	return ret;
}

inline float3 reconstruct_position(in float2 uv, in float z)
{
	return ScreenSpaceToViewSpacePosition(uv, compute_lineardepth(z));
}
