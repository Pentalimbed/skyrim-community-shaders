/*
	Modified from:
	Auroras by nimitz 2017 (twitter: @stormoid)
		License Creative Commons Attribution-NonCommercial-ShareAlike 3.0 Unported License
		url: https://www.shadertoy.com/view/XtGGRt
*/

float2x2 rotMat2x2(in float a)
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
	p = mul(rotMat2x2(p.x * 0.06), p);
	float2 bp = p;
	for (float i = 0.; i < 5.; i++) {
		float2 dg = tri2(bp * 1.85) * .75;
		dg = mul(rotMat2x2(time * spd), dg);
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
float4 aurora(float3 ray_origin, float3 ray_dir, float2 uv, float time)
{
	const float reach = 50.;
	const int steps = 25;
	float stride = reach / steps;

	float4 color = 0;
	float4 color_avg = 0;

	for (int i = 0; i < steps; i++) {
		// get noise coordinates
		float stride_jitter = 0.006 * hash21(uv) * smoothstep(0., 15., i * stride) * stride;
		float distance = ((.8 + pow(i * stride, 1.4) * .002) - ray_origin.y) / (ray_dir.y * 2. + 0.4) - stride_jitter;
		float3 ray_pos = ray_origin + distance * ray_dir;
		float2 noise_pos = ray_pos.zx;

		// march color
		float horizontal_density = triNoise2d(noise_pos, 0.06, time);
		float4 sample_color = float4(sin(1. - float3(2.15, -.5, 1.2) + i * stride * 0.043) * 0.5 + 0.5, 1);
		color_avg = lerp(color_avg, sample_color * horizontal_density, .5);
		color += color_avg * exp2(-i * stride * 0.065 - 2.5) * smoothstep(0., 5., i * stride) * stride;
	}

	// horizon fade/transmittance compensation
	// color *= clamp(ray_dir.y * 15. + .4, 0., 1.);

	return color * 1.8;
}