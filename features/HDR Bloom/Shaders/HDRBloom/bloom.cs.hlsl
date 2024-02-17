// By ProfJack/五脚猫, 2024-2-17 UTC
// ref:
// http://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare

RWTexture2D<float4> RWTexOut : register(u0);

Texture2D<float4> TexColor : register(t0);  // mip level is passed directly by SRV

SamplerState SampColor
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Mirror;
	AddressV = Mirror;
	AddressW = Mirror;
};

cbuffer BloomCB : register(b0)
{
	uint isFirstDownsamplePass;
	float upsampleMult;

	float upsampleRadius;
};

float Luma(float3 color)
{
	return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float4 KarisAverage(float4 a, float4 b, float4 c, float4 d)
{
	float wa = rcp(1 + Luma(a.rgb));
	float wb = rcp(1 + Luma(b.rgb));
	float wc = rcp(1 + Luma(c.rgb));
	float wd = rcp(1 + Luma(d.rgb));
	float wsum = wa + wb + wc + wd;
	return (a * wa + b * wb + c * wc + d * wd) / wsum;
}

// Maybe rewrite as fetch
float4 Downsample(float2 uv, float2 out_px_size)
{
	int x, y;

	float4 retval = 0;
	if (isFirstDownsamplePass) {
		float4 fetches2x2[4];
		float4 fetches3x3[9];

		for (x = 0; x < 2; ++x)
			for (y = 0; y < 2; ++y)
				fetches2x2[x * 2 + y] = TexColor.SampleLevel(SampColor, uv + (int2(x, y) * 2 - 1) * out_px_size, 0);
		for (x = 0; x < 3; ++x)
			for (y = 0; y < 3; ++y)
				fetches3x3[x * 3 + y] = TexColor.SampleLevel(SampColor, uv + (int2(x, y) - 1) * 2 * out_px_size, 0);

		retval += 0.5 * KarisAverage(fetches2x2[0], fetches2x2[1], fetches2x2[2], fetches2x2[3]);

		for (x = 0; x < 2; ++x)
			for (y = 0; y < 2; ++y)
				retval += 0.125 * KarisAverage(fetches3x3[x * 3 + y], fetches3x3[(x + 1) * 3 + y], fetches3x3[x * 3 + y + 1], fetches3x3[(x + 1) * 3 + y + 1]);
	} else {
		for (x = 0; x < 2; ++x)
			for (y = 0; y < 2; ++y)
				retval += 0.125 * TexColor.SampleLevel(SampColor, uv + (int2(x, y) - .5) * out_px_size, 0);

		// const static float weights[9] = { 0.03125, 0.625, 0.03125, 0.625, 0.125, 0.625, 0.03125, 0.625, 0.03125 };
		// corresponds to (1 << (!x + !y)) * 0.03125 when $x,y \in [-1, 1] \cap \mathbb N$
		for (x = -1; x <= 1; ++x)
			for (y = -1; y <= 1; ++y)
				retval += (1 << (!x + !y)) * 0.03125 * TexColor.SampleLevel(SampColor, uv + int2(x, y) * out_px_size, 0);
	}

	return retval;
}

float4 Upsample(float2 uv, float2 radius)
{
	float4 retval = 0;
	for (int x = -1; x <= 1; ++x)
		for (int y = -1; y <= 1; ++y)
			retval += (1 << (!x + !y)) * 0.0625 * TexColor.SampleLevel(SampColor, uv + float2(x, y) * radius, 0);
	return retval;
}

[numthreads(32, 32, 1)] void main(
	uint2 tid : SV_DispatchThreadID, uint2 gid : SV_GroupThreadID) {
	uint2 dims;
	RWTexOut.GetDimensions(dims.x, dims.y);

	float2 px_size = rcp(dims);
	float2 uv = (tid + .5) * px_size;

#if defined(DOWNSAMPLE)
	RWTexOut[tid] = Downsample(uv, px_size);
#else  // upsample
	RWTexOut[tid] = RWTexOut[tid] + Upsample(uv, px_size * upsampleRadius) * upsampleMult;
#endif
}