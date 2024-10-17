#ifndef PHYS_WEATHER_COMMON
#define PHYS_WEATHER_COMMON

const static float PI = 3.1415926535;
const static float RCP_PI = 1.0 / PI;

struct PhaseParams
{
	float g_0;
	float g_1;
	float w;
	float alpha_or_d;  // for Draine or JendersieDEon
};

// unit are in km or km^-1
struct AtmosphereParams
{
	float rayleigh_decay;
	float3 rayleigh_absorption;  //
	float3 rayleigh_scatter;

	float ozone_altitude;  //
	float ozone_thickness;
	float3 ozone_absorption;  //

	float aerosol_decay;
	float3 aerosol_absorption;  //
	float3 aerosol_scatter;
	float _pad;
};

cbuffer PhysWeatherCB : register(b0)
{
	float3 dirlight_dir;
	float cam_height_km;

	float3 ground_albedo;
	float _pad;

	float unit_scale;
	float planet_radius;
	float atmosphere_height;
	float bottom_z;

	AtmosphereParams atmosphere;
};

///////////////////////////////////////////////////////////////////////////////
// GEOMETRIC
///////////////////////////////////////////////////////////////////////////////
float convertGameUnit(float x)  // to km
{
	return x * unit_scale * 1.428e-5;
}

float convertGameHeight(float h)
{
	return convertGameUnit(max(0, h - bottom_z)) + planet_radius;
}

// return distance to sphere surface
// url: https://viclw17.github.io/2018/07/16/raytracing-ray-sphere-intersection
float rayIntersectSphere(float3 orig, float3 dir, float3 center, float r)
{
	float3 oc = orig - center;
	float b = dot(oc, dir);
	float c = dot(oc, oc) - r * r;
	float discr = b * b - c;
	if (discr < 0.0)
		return -1.0;
	// Special case: inside sphere, use far discriminant
	return (discr > b * b) ? (-b + sqrt(discr)) : (-b - sqrt(discr));
}
float rayIntersectSphere(float3 orig, float3 dir, float r)
{
	return rayIntersectSphere(orig, dir, float3(0, 0, 0), r);
}

float3 sphericalDir(float azimuth, float zenith)
{
	float cos_zenith, sin_zenith, cos_azimuth, sin_azimuth;
	sincos(zenith, sin_zenith, cos_zenith);
	sincos(azimuth, sin_azimuth, cos_azimuth);
	return float3(sin_zenith * cos_azimuth, sin_zenith * sin_azimuth, cos_zenith);
}

float getHorizonZenithCos(float altitude)
{
	float sin_zenith = planet_radius / altitude;
	return -sqrt(1 - sin_zenith * sin_zenith);
}

float2 getHeightZenithLutUv(float altitude, float3 up, float3 sun_dir)
{
	float hor_cos_zenith = getHorizonZenithCos(altitude);
	float sun_cos_zenith = dot(sun_dir, up);
	float2 uv = float2(
		saturate((sun_cos_zenith - hor_cos_zenith) / (1 - hor_cos_zenith)),
		saturate((altitude - planet_radius) / atmosphere_height));
	return uv;
}

float2 getHeightZenithLutUv(float altitude, float3 sun_dir)
{
	return getHeightZenithLutUv(altitude, float3(0, 0, 1), sun_dir);
}

float2 getHeightZenithLutUv(float3 pos, float3 sun_dir)
{
	float altitude = length(pos);
	float3 up = pos / altitude;
	return getHeightZenithLutUv(altitude, up, sun_dir);
}

float2 cylinderMapAdjusted(float3 ray_dir)
{
	float azimuth = atan2(ray_dir.y, ray_dir.x);
	float u = azimuth * .5 * RCP_PI;  // sampler wraps around so ok
	float zenith = asin(ray_dir.z);
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

///////////////////////////////////////////////////////////////////////////////
// PHASE FUNCTIONS
///////////////////////////////////////////////////////////////////////////////

float phaseRayleigh(float cos_theta)
{
	const float k = .1875 * RCP_PI;
	return k * (1.0 + cos_theta * cos_theta);
}
float phaseRayleigh(float cos_theta, PhaseParams params)
{
	return phaseRayleigh(cos_theta);
}

float phaseHenyeyGreenstein(float cos_theta, float g)
{
	static const float scale = .25 * RCP_PI;
	const float g2 = g * g;

	float num = (1.0 - g2);
	float denom = pow(abs(1.0 + g2 - 2.0 * g * cos_theta), 1.5);

	return scale * num / denom;
}
float phaseHenyeyGreenstein(float cos_theta, PhaseParams params)
{
	return phaseHenyeyGreenstein(cos_theta, params.g_0);
}

float phaseHenyeyGreensteinDualLobe(float cos_theta, float g_0, float g_1, float w)
{
	return lerp(phaseHenyeyGreenstein(cos_theta, g_0), phaseHenyeyGreenstein(cos_theta, g_1), w);
}
float phaseHenyeyGreensteinDualLobe(float cos_theta, PhaseParams params)
{
	return phaseHenyeyGreensteinDualLobe(cos_theta, params.g_0, params.g_1, params.w);
}

float phaseCornetteShanks(float cos_theta, float g)
{
	static const float scale = .375 * RCP_PI;
	const float g2 = g * g;

	float num = (1.0 - g2) * (1.0 + cos_theta * cos_theta);
	float denom = (2.0 + g2) * pow(abs(1.0 + g2 - 2.0 * g * cos_theta), 1.5);

	return scale * num / denom;
}
float phaseCornetteShanks(float cos_theta, PhaseParams params)
{
	return phaseCornetteShanks(cos_theta, params.g_0);
}

float phaseDraine(float cos_theta, float g, float alpha)
{
	static const float scale = .25 * RCP_PI;
	const float g2 = g * g;

	float num = (1.0 - g2) * (1.0 + alpha * cos_theta * cos_theta);
	float denom = pow(abs(1.0 + g2 - 2.0 * g * cos_theta), 1.5) * (1.0 + alpha * (1.0 + 2.0 * g2) * .333333333333333);

	return scale * num / denom;
}
float phaseDraine(float cos_theta, PhaseParams params)
{
	return phaseDraine(cos_theta, params.g_0, params.alpha_or_d);
}

// An Approximate Mie Scattering Function for Fog and Cloud Rendering, Johannes Jendersie and Eugene d'Eon
// https://research.nvidia.com/labs/rtr/approximate-mie/publications/approximate-mie-supplemental.pdf
float phaseJendersieDEon(float cos_theta, float d)  // d = particle diameter / um
{
	float g_hg, g_d, alpha_d, w_d;
	if (d >= 5) {
		g_hg = exp(-0.09905670 / (d - 1.67154));
		g_d = exp(-2.20679 / (d + 3.91029) - 0.428934);
		alpha_d = exp(3.62489 - 8.29288 / (d + 5.52825));
		w_d = exp(-0.599085 / (d - 0.641583) - 0.665888);
	} else if (d >= 1.5) {
		float logd = log(d);
		float loglogd = log(logd);
		g_hg = 0.0604931 * loglogd + 0.940256;
		g_d = 0.500411 - 0.081287 / (-2 * logd + tan(logd) + 1.27551);
		alpha_d = 7.30354 * logd + 6.31675;
		w_d = 0.026914 * (logd - cos(5.68947 * (loglogd - 0.0292149))) + 0.376475;
	} else if (d > .1) {
		float logd = log(d);
		g_hg = 0.862 - 0.143 * logd * logd;
		g_d = 0.379685 * cos(1.19692 * cos((logd - 0.238604) * (logd + 1.00667) / (0.507522 - 0.15677 * logd)) + 1.37932 * logd + 0.0625835) + 0.344213;
		alpha_d = 250;
		w_d = 0.146209 * cos(3.38707 * logd + 2.11193) + 0.316072 + 0.0778917 * logd;
	} else {
		g_hg = 13.58 * d * d;
		g_d = 1.1456 * d * sin(9.29044 * d);
		alpha_d = 250;
		w_d = 0.252977 - 312.983 * pow(d, 4.3);
	}
	return lerp(phaseHenyeyGreenstein(cos_theta, g_hg), phaseDraine(cos_theta, g_d, alpha_d), w_d);
}
float phaseJendersieDEon(float cos_theta, PhaseParams params)
{
	return phaseJendersieDEon(cos_theta, params.alpha_or_d);
}

// https://www.shadertoy.com/view/tl33Rn
float smoothstep_unchecked(float x) { return (x * x) * (3.0 - x * 2.0); }
float smoothbump(float a, float r, float x) { return 1.0 - smoothstep_unchecked(min(abs(x - a), r) / r); }
float powerful_scurve(float x, float p1, float p2) { return pow(1.0 - pow(1.0 - saturate(x), p2), p1); }
float3 phaseCloudMultiscatter(float cos_theta)
{
	float x = acos(cos_theta);
	float x2 = max(0., x - 2.45) / (PI - 2.15);
	float x3 = max(0., x - 2.95) / (PI - 2.95);
	float y = (exp(-max(x * 1.5 + 0.0, 0.0) * 30.0)         // front peak
			   + smoothstep(1.7, 0., x) * 0.45 * 0.8        // front ramp
			   + smoothbump(0.4, 0.5, cos_theta) * 0.02     // front bump middle
			   - smoothstep(1., 0.2, x) * 0.06              // front ramp damp wave
			   + smoothbump(2.18, 0.20, x) * 0.06           // first trail wave
			   + smoothstep(2.28, 2.45, x) * 0.18           // trailing piece
			   - powerful_scurve(x2 * 4.0, 3.5, 8.) * 0.04  // trail
			   + x2 * -0.085 + x3 * x3 * 0.1);              // trail peak

	float3 ret = y;
	// spectralize a bit
	ret = lerp(ret, ret + 0.008 * 2., smoothstep(0.94, 1., cos_theta) * sin(x * 10. * float3(8, 4, 2)));
	ret = lerp(ret, ret - 0.008 * 2., smoothbump(-0.7, 0.14, cos_theta) * sin(x * 20. * float3(8, 4, 2)));   // fogbow
	ret = lerp(ret, ret - 0.008 * 5., smoothstep(-0.994, -1., cos_theta) * sin(x * 30. * float3(3, 4, 2)));  // glory

	// scale and offset should be tweaked so integral on sphere is 1
	ret += 0.13 * 1.4;
	return ret * 3.9;
}
float phaseCloudMultiscatter(float cos_theta, PhaseParams params)
{
	return phaseCloudMultiscatter(cos_theta);
}

///////////////////////////////////////////////////////////////////////////////
// PARTICIPATING MEDIA
///////////////////////////////////////////////////////////////////////////////

void sampleAtmosphere(
	float altitude_km,  // altitude in km
	AtmosphereParams params,
	out float3 rayleigh_scatter,
	out float3 aerosol_scatter,
	out float3 extinction)
{
	float rayleigh_density = exp(-altitude_km * params.rayleigh_decay);
	rayleigh_scatter = params.rayleigh_scatter * rayleigh_density;
	float3 rayleigh_absorp = params.rayleigh_absorption * rayleigh_density;

	float aerosol_density = exp(-altitude_km * params.aerosol_decay);
	aerosol_scatter = params.aerosol_scatter * aerosol_density;
	float3 aerosol_absorp = params.aerosol_absorption * aerosol_density;

	float ozone_density = max(0, 1 - abs(altitude_km - params.ozone_altitude) / (params.ozone_thickness * 0.5));
	float3 ozone_absorp = params.ozone_absorption * ozone_density;

	extinction = rayleigh_scatter + rayleigh_absorp + aerosol_scatter + aerosol_absorp + ozone_absorp;
}

///////////////////////////////////////////////////////////////////////////////
// BUFFER STRUCTS
///////////////////////////////////////////////////////////////////////////////

#endif