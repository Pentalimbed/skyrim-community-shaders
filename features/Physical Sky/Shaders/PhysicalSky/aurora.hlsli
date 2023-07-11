// Auroras by nimitz 2017 (twitter: @stormoid)
// License Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License
// Contact the author for other licensing options
// src: https://www.shadertoy.com/view/XtGGRt

float2x2 mm2(in float a)
{
	float c, s;
	sincos(a, s, c);
	return float2x2(c, s, -s, c);
}
static const float2x2 m2 = float2x2(0.95534, 0.29552, -0.29552, 0.95534);
float tri(in float x) { return clamp(abs(frac(x) - .5), 0.01, 0.49); }
float2 tri2(in float2 p) { return float2(tri(p.x) + tri(p.y), tri(p.y + tri(p.x))); }

float triNoise2d(in float2 p, float spd, float time)
{
	float z = 1.8;
	float z2 = 2.5;
	float rz = 0.;
	p = mul(mm2(p.x * 0.06), p);
	float2 bp = p;
	for (float i = 0.; i < 5.; i++) {
		float2 dg = tri2(bp * 1.85) * .75;
		dg = mul(mm2(time * spd), dg);
		p -= dg / z2;

		bp *= 1.3;
		z2 *= .45;
		z *= .42;
		p *= 1.21 + (rz - 1.0) * .02;

		rz += tri(p.x + tri(p.y)) * z;
		p = mul(-m2, p);
	}
	return clamp(1. / pow(rz * 29., 1.3), 0., .55);
}

float hash21(in float2 n) { return frac(sin(dot(n, float2(12.9898, 4.1414))) * 43758.5453); }
float4 aurora(float3 ro, float3 rd, float2 uv, float time)
{
	float4 col = 0;
	float4 avgCol = 0;

	for (float i = 0.; i < 50.; i++) {
		float of = 0.006 * hash21(uv) * smoothstep(0., 15., i);
		float pt = ((.8 + pow(i, 1.4) * .002) - ro.y) / (rd.y * 2. + 0.4);
		pt -= of;
		float3 bpos = ro + pt * rd;
		float2 p = bpos.zx;
		float rzt = triNoise2d(p, 0.06, time);
		float4 col2 = float4(0, 0, 0, rzt);
		col2.rgb = (sin(1. - float3(2.15, -.5, 1.2) + i * 0.043) * 0.5 + 0.5) * rzt;
		avgCol = lerp(avgCol, col2, .5);
		col += avgCol * exp2(-i * 0.065 - 2.5) * smoothstep(0., 5., i);
	}

	col *= clamp(rd.y * 15. + .4, 0., 1.);

	return col * 1.8;
}