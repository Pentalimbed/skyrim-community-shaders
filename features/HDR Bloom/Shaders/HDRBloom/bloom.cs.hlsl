/// By ProfJack/五脚猫, 2024-2-28 UTC
/// ref:
/// http://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare
/// https://www.froyok.fr/blog/2021-09-ue4-custom-lens-flare/
/// https://community.arm.com/cfs-file/__key/communityserver-blogs-components-weblogfiles/00-00-00-20-66/siggraph2015_2D00_mmg_2D00_marius_2D00_notes.pdf

#include "common.hlsli"

Texture2D<float4> TexColor : register(t0);
Texture2D<float4> TexBloomIn : register(t1);
Texture2D<float4> TexGhostsIn : register(t2);

#define N_GHOSTS 9
struct GhostParameters
{
	uint Mip;
	float Scale;
	float Intensity;
	float Chromatic;
};
StructuredBuffer<GhostParameters> GhostParametersSB : register(t3);

RWTexture2D<float4> RWTexBloomOut : register(u0);
RWTexture2D<float4> RWTexGhostsOut : register(u1);

cbuffer BloomCB : register(b0)
{
	// threshold
	float2 Thresholds : packoffset(c0.x);
	// downsample
	uint IsFirstMip : packoffset(c0.z);
	// upsample
	float UpsampleRadius : packoffset(c0.w);
	float UpsampleMult : packoffset(c1.x);  // in composite: bloom mult
	float CurrentMipMult : packoffset(c1.y);
	// ghosts
	float GhostsCentralSize : packoffset(c1.z);
	// composite
	float NaturalVignetteFocal : packoffset(c1.w);
	float NaturalVignettePower : packoffset(c2.x);
};

SamplerState SampColor : register(s0);

float3 Sanitise(float3 v)
{
	bool3 err = IsNaN(v) || (v < 0);
	v.x = err.x ? 0 : v.x;
	v.y = err.y ? 0 : v.y;
	v.z = err.z ? 0 : v.z;
	return v;
}

float3 ThresholdColor(float3 col, float threshold)
{
	float luma = Luma(col);
	if (luma < 1e-3)
		return 0;
	return col * (max(0, luma - threshold) / luma);
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
float4 DownsampleCOD(Texture2D tex, float2 uv, float2 out_px_size)
{
	int x, y;

	float4 retval = 0;
	if (IsFirstMip) {
		float4 fetches2x2[4];
		float4 fetches3x3[9];

		for (x = 0; x < 2; ++x)
			for (y = 0; y < 2; ++y)
				fetches2x2[x * 2 + y] = tex.SampleLevel(SampColor, uv + (int2(x, y) * 2 - 1) * out_px_size, 0);
		for (x = 0; x < 3; ++x)
			for (y = 0; y < 3; ++y)
				fetches3x3[x * 3 + y] = tex.SampleLevel(SampColor, uv + (int2(x, y) - 1) * 2 * out_px_size, 0);

		retval += 0.5 * KarisAverage(fetches2x2[0], fetches2x2[1], fetches2x2[2], fetches2x2[3]);

		for (x = 0; x < 2; ++x)
			for (y = 0; y < 2; ++y)
				retval += 0.125 * KarisAverage(fetches3x3[x * 3 + y], fetches3x3[(x + 1) * 3 + y], fetches3x3[x * 3 + y + 1], fetches3x3[(x + 1) * 3 + y + 1]);
	} else {
		for (x = 0; x < 2; ++x)
			for (y = 0; y < 2; ++y)
				retval += 0.125 * tex.SampleLevel(SampColor, uv + (int2(x, y) - .5) * out_px_size, 0);

		// const static float weights[9] = { 0.03125, 0.625, 0.03125, 0.625, 0.125, 0.625, 0.03125, 0.625, 0.03125 };
		// corresponds to (1 << (!x + !y)) * 0.03125 when $x,y \in [-1, 1] \cap \mathbb N$
		for (x = -1; x <= 1; ++x)
			for (y = -1; y <= 1; ++y)
				retval += (1u << (!x + !y)) * 0.03125 * tex.SampleLevel(SampColor, uv + int2(x, y) * out_px_size, 0);
	}

	return retval;
}

float4 UpsampleCOD(Texture2D tex, float2 uv, float2 radius)
{
	float4 retval = 0;
	for (int x = -1; x <= 1; ++x)
		for (int y = -1; y <= 1; ++y)
			retval += (1 << (!x + !y)) * 0.0625 * tex.SampleLevel(SampColor, uv + float2(x, y) * radius, 0);
	return retval;
}

float3 SampleChromatic(Texture2D tex, float2 uv, uint mip, float chromatic)
{
	float3 col;
	col.r = tex.SampleLevel(SampColor, lerp(.5, uv, 1 - chromatic), mip).r;
	col.g = tex.SampleLevel(SampColor, uv, mip).g;
	col.b = tex.SampleLevel(SampColor, lerp(.5, uv, 1 + chromatic), mip).b;
	return col;
}

[numthreads(32, 32, 1)] void CS_Threshold(uint2 tid : SV_DispatchThreadID) {
	float3 col_input = TexColor[tid].rgb;

	float3 col = col_input;
	col = Sanitise(col);
	col = ThresholdColor(col, Thresholds.x);
	RWTexBloomOut[tid] = float4(col, 1);

	col = col_input;
	col = Sanitise(col);
	col = ThresholdColor(col, Thresholds.y);
	RWTexGhostsOut[tid] = float4(col, 1);
};

[numthreads(32, 32, 1)] void CS_Downsample(uint2 tid : SV_DispatchThreadID) {
	uint2 dims;
	RWTexBloomOut.GetDimensions(dims.x, dims.y);

	float2 px_size = rcp(dims);
	float2 uv = (tid + .5) * px_size;

#ifdef BLOOM
	{
		float3 col = DownsampleCOD(TexBloomIn, uv, px_size).rgb;
		RWTexBloomOut[tid] = float4(col, 1);
	}
#endif

#ifdef GHOSTS
	{
		float3 col = DownsampleCOD(TexGhostsIn, uv, px_size).rgb;
		RWTexGhostsOut[tid] = float4(col, 1);
	}
#endif
};

[numthreads(32, 32, 1)] void CS_Ghosts(uint2 tid : SV_DispatchThreadID) {
	uint2 dims;
	RWTexGhostsOut.GetDimensions(dims.x, dims.y);

	float2 px_size = rcp(dims);
	float2 uv = (tid + .5) * px_size;

	float3 col = 0;
	for (uint i = 0; i < N_GHOSTS; ++i)
		if (GhostParametersSB[i].Intensity > 1e-3) {
			float3 ghost = 0;

			uint mip = GhostParametersSB[i].Mip;
			float scale = rcp(GhostParametersSB[i].Scale);
			float intensity = GhostParametersSB[i].Intensity;
			float chromatic = GhostParametersSB[i].Chromatic;

			float2 sampleUV = .5 + (.5 - uv) * scale;

			if (abs(chromatic) > 1e-3)
				ghost = SampleChromatic(TexGhostsIn, sampleUV, mip, chromatic);
			else
				ghost = TexGhostsIn.SampleLevel(SampColor, sampleUV, mip).rgb;

			// only central weights
			ghost *= 1 - smoothstep(0, GhostsCentralSize, length(sampleUV - .5));

			col += ghost * intensity;
		}

	RWTexGhostsOut[tid] = float4(col, 1);
};

[numthreads(32, 32, 1)] void CS_Upsample(uint2 tid : SV_DispatchThreadID) {
	uint2 dims;
	RWTexBloomOut.GetDimensions(dims.x, dims.y);

	float2 px_size = rcp(dims);
	float2 uv = (tid + .5) * px_size;

	float3 col = RWTexBloomOut[tid].rgb * CurrentMipMult + UpsampleCOD(TexBloomIn, uv, px_size * UpsampleRadius).rgb * UpsampleMult;
	RWTexBloomOut[tid] = float4(col, 1);
};

[numthreads(32, 32, 1)] void CS_Composite(uint2 tid : SV_DispatchThreadID) {
	uint2 dims;
	RWTexBloomOut.GetDimensions(dims.x, dims.y);

	float2 px_size = rcp(dims);
	float2 uv = (tid + .5) * px_size;

	float3 col = TexColor[tid].rgb +
	             UpsampleCOD(TexBloomIn, uv, px_size * UpsampleRadius).rgb * UpsampleMult +
	             TexGhostsIn.SampleLevel(SampColor, uv, 0).rgb;

	// natural vignette
	if (NaturalVignettePower > 1e-3f) {
		float cos_view = length(uv - .5);
		cos_view = NaturalVignetteFocal * rsqrt(cos_view * cos_view + NaturalVignetteFocal * NaturalVignetteFocal);
		float vignette = pow(cos_view, NaturalVignettePower);

		col *= vignette;
	}

	RWTexBloomOut[tid] = float4(col, 1);
};