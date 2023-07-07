/*
	Adapted from: https://www.shadertoy.com/view/slSXRW
	It is said to be MIT license by the author in the comment section, but the license text is nowhere to be found.
*/

#ifndef PHYS_SKY_NO_PI
static const float PI = 3.141592653589793238462643383279;
#endif

struct PhysSkySB
{
	float3 sun_dir;
	float3 player_cam_pos;

	bool enable_sky;
	bool enable_scatter;

	uint transmittance_step;
	uint multiscatter_step;
	uint multiscatter_sqrt_samples;
	uint skyview_step;
	float aerial_perspective_max_dist;

	float2 unit_scale;
	float bottom_z;
	float ground_radius;
	float atmos_thickness;

	float3 ground_albedo;

	float3 source_illuminance;

	uint limb_darken_model;
	float3 sun_luminance;
	float sun_half_angle;

	float3 rayleigh_scatter;
	float3 rayleigh_absorption;
	float rayleigh_decay;

	float3 mie_scatter;
	float3 mie_absorption;
	float mie_decay;

	float3 ozone_absorption;
	float ozone_height;
	float ozone_thickness;

	float ap_inscatter_mix;
	float ap_transmittance_mix;
	float light_transmittance_mix;
};

struct PerCameraSB
{
	float3 eye_pos;
	float4x4 inv_view;
};

// return distance to sphere surface
// src:
// https://gamedev.stackexchange.com/questions/96459/fast-ray-sphere-collision-code.
float rayIntersectSphere(float3 orig, float3 dir, float rad)
{
	float b = dot(orig, dir);
	float c = dot(orig, orig) - rad * rad;
	if (c > 0.0f && b > 0.0)
		return -1.0;
	float discr = b * b - c;
	if (discr < 0.0)
		return -1.0;
	// Special case: inside sphere, use far discriminant
	if (discr > b * b)
		return (-b + sqrt(discr));
	return -b - sqrt(discr);
}

float3 getSphericalDir(float azimuth, float zenith)
{
	float cos_zenith, sin_zenith, cos_azimuth, sin_azimuth;
	sincos(zenith, sin_zenith, cos_zenith);
	sincos(azimuth, sin_azimuth, cos_azimuth);
	return float3(sin_zenith * sin_azimuth, sin_zenith * cos_azimuth, cos_zenith);
}

void scatterValues(
	float3 pos,  // position relative to the center of the planet, in megameter
	PhysSkySB sky,
	out float3 rayleigh_scatter,
	out float3 mie_scatter,
	out float3 extinction)
{
	float altitude_km = (length(pos) - sky.ground_radius) * 1000.0;
	float rayleigh_density = exp(-altitude_km / sky.rayleigh_decay);
	float mie_density = exp(-altitude_km / sky.mie_decay);
	float ozone_density = max(0, 1 - abs(altitude_km - sky.ozone_height) / (sky.ozone_thickness * 0.5));

	rayleigh_scatter = sky.rayleigh_scatter * rayleigh_density;
	float3 rayleigh_absorption = sky.rayleigh_absorption * rayleigh_density;

	mie_scatter = sky.mie_scatter * mie_density;
	float3 mie_absorp = sky.mie_absorption * mie_density;

	float3 ozone_absorp = sky.ozone_absorption * ozone_density;

	extinction = rayleigh_scatter + rayleigh_absorption + mie_scatter + mie_absorp + ozone_absorp;
}

float miePhase(float cos_theta)
{
	const float g = 0.8;
	const float scale = 3.0 / (8.0 * PI);

	float num = (1.0 - g * g) * (1.0 + cos_theta * cos_theta);
	float denom = (2.0 + g * g) * pow(abs(1.0 + g * g - 2.0 * g * cos_theta), 1.5);

	return scale * num / denom;
}

float rayleighPhase(float cos_theta)
{
	const float k = 3.0 / (16.0 * PI);
	return k * (1.0 + cos_theta * cos_theta);
}

float2 getLutUv(float3 pos, float3 sun_dir, float ground_radius, float atmos_thickness)
{
	float height = length(pos);
	float3 up = pos / height;
	float sun_cos_zenith = dot(sun_dir, up);
	float2 uv = float2(saturate(0.5 + 0.5 * sun_cos_zenith), saturate((height - ground_radius) / atmos_thickness));
	return uv;
}

float2 getSkyLutUv(float3 dir)
{
	float azimuth = atan2(dir.x, dir.y);
	float u = azimuth / (2 * PI);
	float zenith = asin(dir.z);
	if (abs(zenith) > (0.5 * PI - .0001))
		zenith = 0.0;
	float v = 0.5 - 0.5 * sign(zenith) * sqrt(abs(zenith) * 2 / PI);
	return float2(u, v);
}

float3 limbDarkenNeckel(float norm_dist)
{
	// Model from http://www.physics.hmc.edu/faculty/esin/a101/limbdarkening.pdf
	float3 u = float3(1.0, 1.0, 1.0);        // some models have u !=1
	float3 a = float3(0.397, 0.503, 0.652);  // coefficient for RGB wavelength (680 ,550 ,440)

	float mu = sqrt(1.0 - norm_dist * norm_dist);

	float3 factor = 1.0 - u * (1.0 - pow(mu, a));
	return factor;
}

float3 limbDarkenHestroffer(float norm_dist)
{
	// Model using P5 polynomial from http://articles.adsabs.harvard.edu/cgi-bin/nph-iarticle_query?1994SoPh..153...91N&defaultprint=YES&filetype=.pdf
	float mu = sqrt(1.0 - norm_dist * norm_dist);

	// coefficient for RGB wavelength (680 ,550 ,440)
	float3 a0 = float3(0.34685, 0.26073, 0.15248);
	float3 a1 = float3(1.37539, 1.27428, 1.38517);
	float3 a2 = float3(-2.04425, -1.30352, -1.49615);
	float3 a3 = float3(2.70493, 1.47085, 1.99886);
	float3 a4 = float3(-1.94290, -0.96618, -1.48155);
	float3 a5 = float3(0.55999, 0.26384, 0.44119);

	float mu2 = mu * mu;
	float mu3 = mu2 * mu;
	float mu4 = mu2 * mu2;
	float mu5 = mu4 * mu;

	float3 factor = a0 + a1 * mu + a2 * mu2 + a3 * mu3 + a4 * mu4 + a5 * mu5;
	return factor;
}

float3 jodieReinhardTonemap(float3 c)
{
	// From: https://www.shadertoy.com/view/tdSXzD
	float l = dot(c, float3(0.2126, 0.7152, 0.0722));
	float3 tc = c / (c + 1.0);
	return saturate(lerp(c / (l + 1.0), tc, tc));
}

float rgbLuminance(float3 color)
{
	return 0.2126 * color.r + 0.7152 * color.g + 0.0722 * color.b;
}