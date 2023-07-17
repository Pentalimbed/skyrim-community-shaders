#ifndef phys_weather_NO_PI
static const float PI = 3.141592653589793238462643383279;
#endif
static const float RCP_PI = 1.0 / PI;

SamplerState SampSkyView
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Wrap;
	AddressV = Mirror;
	AddressU = Mirror;
};

SamplerState MirrorLinearSampler : register(s1)
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Mirror;
	AddressV = Mirror;
	AddressW = Mirror;
};

struct PhysWeatherSB
{
	float timer;
	float3 sun_dir;
	float3 masser_dir;
	float3 masser_upvec;
	float3 secunda_dir;
	float3 secunda_upvec;
	float3 player_cam_pos;

	uint enable_sky;
	uint enable_scatter;
	uint enable_tonemap;
	float tonemap_keyval;

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

	float3 dirlight_color;
	float3 dirlight_dir;

	uint limb_darken_model;
	float limb_darken_power;
	float3 sun_color;
	float sun_aperture_cos;

	float masser_aperture_cos;
	float masser_brightness;

	float secunda_aperture_cos;
	float secunda_brightness;

	float stars_brightness;

	float3 rayleigh_scatter;
	float3 rayleigh_absorption;
	float rayleigh_decay;

	uint mie_phase_func;
	float mie_asymmetry;
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

/*-------- GEOMETRIC --------*/
// return distance to sphere surface
// url: https://gamedev.stackexchange.com/questions/96459/fast-ray-sphere-collision-code.
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

float3 sphericalDir(float azimuth, float zenith)
{
	float cos_zenith, sin_zenith, cos_azimuth, sin_azimuth;
	sincos(zenith, sin_zenith, cos_zenith);
	sincos(azimuth, sin_azimuth, cos_azimuth);
	return float3(sin_zenith * cos_azimuth, sin_zenith * sin_azimuth, cos_zenith);
}

/*-------- VOLUMETRIC --------*/
// url: https://www.shadertoy.com/view/slSXRW

void scatterValues(
	float3 pos,  // position relative to the center of the planet, in megameter
	PhysWeatherSB sky,
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

float miePhaseHenyeyGreenstein(float cos_theta, float g)
{
	const float scale = .25 * RCP_PI;

	float num = (1.0 - g * g);
	float denom = pow(abs(1.0 + g * g - 2.0 * g * cos_theta), 1.5);

	return scale * num / denom;
}

float miePhaseCornetteShanks(float cos_theta, float g)
{
	const float scale = .375 * RCP_PI;

	float num = (1.0 - g * g) * (1.0 + cos_theta * cos_theta);
	float denom = (2.0 + g * g) * pow(abs(1.0 + g * g - 2.0 * g * cos_theta), 1.5);

	return scale * num / denom;
}

float miePhase(float cos_theta, float g, uint func)
{
	if (func == 0)
		return miePhaseHenyeyGreenstein(cos_theta, g);
	else if (func == 1)
		return miePhaseCornetteShanks(cos_theta, g);
	return .5 * RCP_PI;
}

float rayleighPhase(float cos_theta)
{
	const float k = .1875 * RCP_PI;
	return k * (1.0 + cos_theta * cos_theta);
}

/*-------- PROJECTION --------*/
float2 getLutUv(float3 pos, float3 sun_dir, float ground_radius, float atmos_thickness)
{
	float height = length(pos);
	float3 up = pos / height;
	float sun_cos_zenith = dot(sun_dir, up);
	float2 uv = float2(saturate(0.5 + 0.5 * sun_cos_zenith), saturate((height - ground_radius) / atmos_thickness));
	return frac(uv);
}

float2 cylinderMap(float3 view_dir)
{
	float azimuth = atan2(view_dir.y, view_dir.x);
	float u = azimuth * .5 * RCP_PI;
	float zenith = acos(view_dir.z);
	float v = zenith * RCP_PI;
	// v = max(v, 0.01);
	return frac(float2(u, v));
}

float2 cylinderMapAdjusted(float3 view_dir)
{
	float azimuth = atan2(view_dir.y, view_dir.x);
	float u = azimuth * .5 * RCP_PI;
	float zenith = asin(view_dir.z);
	float v = 0.5 - 0.5 * sign(zenith) * sqrt(abs(zenith) * 2 * RCP_PI);
	v = max(v, 0.01);
	return frac(float2(u, v));
}

float3 invCylinderMapAdjusted(float2 uv)
{
	float azimuth = uv.x * 2 * PI;
	float vm = 1 - 2 * uv.y;
	float zenith = PI * .5 * (1 - sign(vm) * vm * vm);
	return sphericalDir(azimuth, zenith);
}

// adjusted lambert azimuthal
// url: https://arxiv.org/vc/arxiv/papers/1206/1206.2068v1.pdf
float3 invLambAzAdjusted(float2 uv, float equator)
{
	const float k_e = tan(PI * .25 + equator * .5);

	const float long = atan2(uv.y - .5, uv.x - .5);

	const float r = length(uv - 0.5) * 2;
	const float temp_0 = 2 * r * r - 1;
	const float lat = atan2(k_e * temp_0, sqrt(1 - temp_0 * temp_0)) + PI * .5;
	return sphericalDir(long, lat);
}

float2 lambAzAdjusted(float3 view_dir, float equator)
{
	const float k_e = tan(PI * .25 + equator * .5);

	const float len_xy = length(view_dir.xy);
	if (len_xy < 1e-10)
		return float2(.5, .5);

	const float tan_lat = -view_dir.z / len_xy;
	const float temp_0 = sign(k_e) * tan_lat * rsqrt(tan_lat * tan_lat + k_e * k_e);
	const float r = sqrt((temp_0 + 1) * .5);
	return .5 + r * normalize(view_dir.xy) * .5;
}

/*-------- SUN LIMB DARKENING --------*/
// url: http://www.physics.hmc.edu/faculty/esin/a101/limbdarkening.pdf
float3 limbDarkenNeckel(float norm_dist)
{
	float3 u = 1.0;                          // some models have u !=1
	float3 a = float3(0.397, 0.503, 0.652);  // coefficient for RGB wavelength (680, 550, 440)

	float mu = sqrt(1.0 - norm_dist * norm_dist);

	float3 factor = 1.0 - u * (1.0 - pow(mu, a));
	return factor;
}

// url: http://articles.adsabs.harvard.edu/cgi-bin/nph-iarticle_query?1994SoPh..153...91N&defaultprint=YES&filetype=.pdf
float3 limbDarkenHestroffer(float norm_dist)
{
	float mu = sqrt(1.0 - norm_dist * norm_dist);

	// coefficient for RGB wavelength (680, 550, 440)
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

float3 limbDarken(float norm_dist, uint model)
{
	if (model == 1)
		return limbDarkenNeckel(norm_dist);
	else if (model == 2)
		return limbDarkenHestroffer(norm_dist);
	return 1;
}

/*-------- COLOR SPACE --------*/
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