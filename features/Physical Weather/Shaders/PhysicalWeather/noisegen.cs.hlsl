/*
	Tileable Perlin-Worley 3D
	url: https://www.shadertoy.com/view/3dVXDc

	Procedural Cloudscapes (supplementary material)
	url: http://webanck.fr/publications/Webanck2018-supplementary_material.pdf
*/

RWTexture3D<float4> tex_noise : register(u0);

// Hash by David_Hoskins
#define UI0 1597334673U
#define UI1 3812015801U
#define UI2 uint2(UI0, UI1)
#define UI3 uint3(UI0, UI1, 2798796415U)
#define UIF (1.0 / float(0xffffffffU))

#define mod(x, y) ((x) - (y)*floor((x) / (y)))

float3 hash33(float3 p)
{
	uint3 q = uint3(int3(p)) * UI3;
	q = (q.x ^ q.y ^ q.z) * UI3;
	return -1. + 2. * float3(q) * UIF;
}

// Gradient noise by iq (modified to be tileable)
float gradientNoise(float3 x, float freq)
{
	// grid
	float3 p = floor(x);
	float3 w = frac(x);

	// quintic interpolant
	float3 u = w * w * w * (w * (w * 6. - 15.) + 10.);

	// gradients
	float3 ga = hash33(mod(p + float3(0., 0., 0.), freq));
	float3 gb = hash33(mod(p + float3(1., 0., 0.), freq));
	float3 gc = hash33(mod(p + float3(0., 1., 0.), freq));
	float3 gd = hash33(mod(p + float3(1., 1., 0.), freq));
	float3 ge = hash33(mod(p + float3(0., 0., 1.), freq));
	float3 gf = hash33(mod(p + float3(1., 0., 1.), freq));
	float3 gg = hash33(mod(p + float3(0., 1., 1.), freq));
	float3 gh = hash33(mod(p + float3(1., 1., 1.), freq));

	// projections
	float va = dot(ga, w - float3(0., 0., 0.));
	float vb = dot(gb, w - float3(1., 0., 0.));
	float vc = dot(gc, w - float3(0., 1., 0.));
	float vd = dot(gd, w - float3(1., 1., 0.));
	float ve = dot(ge, w - float3(0., 0., 1.));
	float vf = dot(gf, w - float3(1., 0., 1.));
	float vg = dot(gg, w - float3(0., 1., 1.));
	float vh = dot(gh, w - float3(1., 1., 1.));

	// interpolation
	return va +
	       u.x * (vb - va) +
	       u.y * (vc - va) +
	       u.z * (ve - va) +
	       u.x * u.y * (va - vb - vc + vd) +
	       u.y * u.z * (va - vc - ve + vg) +
	       u.z * u.x * (va - vb - ve + vf) +
	       u.x * u.y * u.z * (-va + vb + vc - vd + ve - vf - vg + vh);
}

// Tileable 3D worley noise
float worleyNoise(float3 uv, float freq)
{
	float3 id = floor(uv);
	float3 p = frac(uv);

	float minDist = 10000.;
	for (float x = -1.; x <= 1.; ++x) {
		for (float y = -1.; y <= 1.; ++y) {
			for (float z = -1.; z <= 1.; ++z) {
				float3 offset = float3(x, y, z);
				float3 h = hash33(mod(id + offset, freq)) * .5 + .5;
				h += offset;
				float3 d = p - h;
				minDist = min(minDist, dot(d, d));
			}
		}
	}

	// inverted worley noise
	return 1. - minDist;
}

float gradientFbm(float3 uvw, uint octaves, float persistence, float lacunarity)
{
	float mix = 1, freq = 5, val = 0, mix_sum = 0;
	for (uint i = 0; i < octaves; ++i) {
		mix_sum += mix;
		val += mix * (gradientNoise(uvw * freq, freq) * .5 + .5);
		mix *= persistence;
		freq *= lacunarity;
	}

	return val / mix_sum;
}

float worleyFbm(float3 uvw, uint octaves, float persistence, float lacunarity)
{
	float mix = 1, freq = 5, val = 0, mix_sum = 0;
	for (uint i = 0; i < octaves; ++i) {
		mix_sum += mix;
		val += mix * worleyNoise(uvw * freq, freq);
		mix *= persistence;
		freq *= lacunarity;
	}

	return val / mix_sum;
}

[numthreads(8, 8, 8)] void main(uint3 tid
								: SV_DispatchThreadID) {
	uint3 out_dims;
	tex_noise.GetDimensions(out_dims.x, out_dims.y, out_dims.z);
	float3 uvw = (tid + 0.5) / out_dims;

	tex_noise[tid] = float4(gradientFbm(uvw, 8, 0.50, 2.0), worleyFbm(uvw, 4, 0.33, 3), 0, 0);
}