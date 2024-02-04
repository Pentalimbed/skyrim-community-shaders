struct PhySkyBufferContent
{
	uint enable_sky;
	uint enable_aerial;

	// PERFORMANCE
	uint transmittance_step;
	uint multiscatter_step;
	uint multiscatter_sqrt_samples;
	uint skyview_step;
	float aerial_perspective_max_dist;

	// WORLD
	float unit_scale;
	float bottom_z;
	float planet_radius;
	float atmos_thickness;
	float3 ground_albedo;

	// LIGHTING
	uint override_dirlight_color;
	float dirlight_transmittance_mix;
	float treelod_saturation;
	float treelod_mult;

	uint enable_vanilla_clouds;
	float cloud_height;
	float cloud_saturation;
	float cloud_mult;
	float cloud_atmos_scatter;

	float cloud_phase_g0;
	float cloud_phase_g1;
	float cloud_phase_w;
	float cloud_alpha_heuristics;
	float cloud_color_heuristics;

	// CELESTIAL
	uint override_vanilla_celestials;

	float3 sun_disc_color;
	float sun_aperture_cos;
	float sun_aperture_rcp_sin;

	float masser_aperture_cos;
	float masser_brightness;

	float secunda_aperture_cos;
	float secunda_brightness;

	// ATMOSPHERE
	float ap_inscatter_mix;
	float ap_transmittance_mix;

	float3 rayleigh_scatter;
	float3 rayleigh_absorption;
	float rayleigh_decay;

	float aerosol_phase_func_g;
	float3 aerosol_scatter;
	float3 aerosol_absorption;
	float aerosol_decay;

	float3 ozone_absorption;
	float ozone_altitude;
	float ozone_thickness;

	// DYNAMIC
	float3 dirlight_dir;
	float3 dirlight_color;
	float3 sun_dir;
	float3 masser_dir;
	float3 masser_upvec;
	float3 secunda_dir;
	float3 secunda_upvec;

	float horizon_penumbra;

	float cam_height_km;
};

struct SkyPerGeometrySB
{
	uint sky_object_type;
};

#ifdef SKY_SAMPLERS
SamplerState TransmittanceSampler : register(s3);  // in lighting, use shadow
SamplerState SkyViewSampler : register(s4);        // in lighting, use color
#endif

#ifdef LUTGEN
StructuredBuffer<PhySkyBufferContent> PhysSkyBuffer : register(t0);
Texture2D<float4> TexTransmittance : register(t1);
Texture2D<float4> TexMultiScatter : register(t2);
#else
StructuredBuffer<PhySkyBufferContent> PhysSkyBuffer : register(t50);
Texture2D<float4> TexTransmittance : register(t51);
Texture2D<float4> TexMultiScatter : register(t52);
Texture2D<float4> TexSkyView : register(t53);
Texture3D<float4> TexAerialPerspective : register(t54);
StructuredBuffer<SkyPerGeometrySB> SkyPerGeometryBuffer : register(t55);
Texture2D<float4> TexMasser : register(t56);
Texture2D<float4> TexSecunda : register(t57);
#endif

#ifndef PI
#	define PI 3.1415927
#endif
#ifndef RCP_PI
#	define RCP_PI 0.3183099
#endif

/*-------- GEOMETRIC --------*/
float convertGameUnit(float x)
{
	return x * PhysSkyBuffer[0].unit_scale * 1.428e-5;
}

float convertGameHeight(float h)
{
	return convertGameUnit(max(0, h - PhysSkyBuffer[0].bottom_z)) + PhysSkyBuffer[0].planet_radius;
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
	float sin_zenith = PhysSkyBuffer[0].planet_radius / altitude;
	return -sqrt(1 - sin_zenith * sin_zenith);
}

float2 getHeightZenithLutUv(float altitude, float3 up, float3 sun_dir)
{
	float hor_cos_zenith = getHorizonZenithCos(altitude);
	float sun_cos_zenith = dot(sun_dir, up);
	float2 uv = float2(
		saturate((sun_cos_zenith - hor_cos_zenith) / (1 - hor_cos_zenith)),
		saturate((altitude - PhysSkyBuffer[0].planet_radius) / PhysSkyBuffer[0].atmos_thickness));
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

/*-------- VOLUMETRIC --------*/
void getScatterValues(
	float height,  // relative to the center of the planet, in megameter
	out float3 rayleigh_scatter,
	out float3 aerosol_scatter,
	out float3 extinction)
{
	float altitude_km = max(0, (height - PhysSkyBuffer[0].planet_radius));
	float rayleigh_density = exp(-altitude_km * PhysSkyBuffer[0].rayleigh_decay);
	float aerosol_density = exp(-altitude_km * PhysSkyBuffer[0].aerosol_decay);
	float ozone_density = max(0, 1 - abs(altitude_km - PhysSkyBuffer[0].ozone_altitude) / (PhysSkyBuffer[0].ozone_thickness * 0.5));

	rayleigh_scatter = PhysSkyBuffer[0].rayleigh_scatter * rayleigh_density;
	float3 rayleigh_absorp = PhysSkyBuffer[0].rayleigh_absorption * rayleigh_density;

	aerosol_scatter = PhysSkyBuffer[0].aerosol_scatter * aerosol_density;
	float3 aerosol_absorp = PhysSkyBuffer[0].aerosol_absorption * aerosol_density;

	float3 ozone_absorp = PhysSkyBuffer[0].ozone_absorption * ozone_density;

	extinction = rayleigh_scatter + rayleigh_absorp + aerosol_scatter + aerosol_absorp + ozone_absorp;
}

float miePhaseHenyeyGreenstein(float cos_theta, float g)
{
	static const float scale = .25 * RCP_PI;
	const float g2 = g * g;

	float num = (1.0 - g2);
	float denom = pow(abs(1.0 + g2 - 2.0 * g * cos_theta), 1.5);

	return scale * num / denom;
}

float miePhaseHenyeyGreensteinDualLobe(float cos_theta, float g_0, float g_1, float w)
{
	return lerp(miePhaseHenyeyGreenstein(cos_theta, g_0), miePhaseHenyeyGreenstein(cos_theta, g_1), w);
}

float miePhaseCornetteShanks(float cos_theta, float g)
{
	static const float scale = .375 * RCP_PI;
	const float g2 = g * g;

	float num = (1.0 - g2) * (1.0 + cos_theta * cos_theta);
	float denom = (2.0 + g2) * pow(abs(1.0 + g2 - 2.0 * g * cos_theta), 1.5);

	return scale * num / denom;
}

float miePhaseDraine(float cos_theta, float g, float alpha)
{
	static const float scale = .25 * RCP_PI;
	const float g2 = g * g;

	float num = (1.0 - g2) * (1.0 + alpha * cos_theta * cos_theta);
	float denom = pow(abs(1.0 + g2 - 2.0 * g * cos_theta), 1.5) * (1.0 + alpha * (1.0 + 2.0 * g2) * .333333333333333);

	return scale * num / denom;
}

float miePhaseJendersieDEon(float cos_theta, float d)  // d = particle diameter / um
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
	return lerp(miePhaseHenyeyGreenstein(cos_theta, g_hg), miePhaseDraine(cos_theta, g_d, alpha_d), w_d);
}

// https://www.shadertoy.com/view/tl33Rn
float smoothstep_unchecked(float x) { return (x * x) * (3.0 - x * 2.0); }
float smoothbump(float a, float r, float x) { return 1.0 - smoothstep_unchecked(min(abs(x - a), r) / r); }
float powerful_scurve(float x, float p1, float p2) { return pow(1.0 - pow(1.0 - saturate(x), p2), p1); }
float3 miePhaseCloudMultiscatter(float cos_theta)
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

float rayleighPhase(float cos_theta)
{
	const float k = .1875 * RCP_PI;
	return k * (1.0 + cos_theta * cos_theta);
}

/*-------- SUN LIMB DARKENING --------*/
// url: http://articles.adsabs.harvard.edu/cgi-bin/nph-iarticle_query?1994SoPh..153...91N&defaultprint=YES&filetype=.pdf
float3 limbDarkenNeckel(float norm_dist)
{
	float3 u = 1.0;                          // some models have u !=1
	float3 a = float3(0.397, 0.503, 0.652);  // coefficient for RGB wavelength (680, 550, 440)

	float mu = sqrt(1.0 - norm_dist * norm_dist);

	float3 factor = 1.0 - u * (1.0 - pow(mu, a));
	return factor;
}

// url: http://www.physics.hmc.edu/faculty/esin/a101/limbdarkening.pdf
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

#ifndef LUTGEN
float4 getLightingApSample(float3 world_pos_unadjusted, SamplerState samp)
{
	if (PhysSkyBuffer[0].enable_sky && PhysSkyBuffer[0].enable_aerial) {
		uint3 ap_dims;
		TexAerialPerspective.GetDimensions(ap_dims.x, ap_dims.y, ap_dims.z);

		float dist = length(world_pos_unadjusted);
		float3 view_dir = world_pos_unadjusted / dist;
		dist = convertGameUnit(dist);

		float depth_slice = lerp(.5 / ap_dims.z, 1 - .5 / ap_dims.z, saturate(dist / PhysSkyBuffer[0].aerial_perspective_max_dist));

		float4 ap_sample = TexAerialPerspective.SampleLevel(samp, float3(cylinderMapAdjusted(view_dir), depth_slice), 0);
		ap_sample.rgb *= PhysSkyBuffer[0].dirlight_color * PhysSkyBuffer[0].ap_inscatter_mix;
		ap_sample.w = lerp(1, ap_sample.w, PhysSkyBuffer[0].ap_transmittance_mix);

		return ap_sample;
	}

	return float4(0, 0, 0, 1);
}

float3 getLightingTransmitSample(float world_pos_unadjusted_z, SamplerState samp)
{
	if (PhysSkyBuffer[0].enable_sky && PhysSkyBuffer[0].dirlight_transmittance_mix > 1e-3) {
		float2 lut_uv = getHeightZenithLutUv(PhysSkyBuffer[0].cam_height_km + convertGameUnit(world_pos_unadjusted_z), PhysSkyBuffer[0].dirlight_dir);  // ignore height change
		float3 transmit_sample = TexTransmittance.SampleLevel(samp, lut_uv, 0).rgb;
		transmit_sample = lerp(1, transmit_sample, PhysSkyBuffer[0].dirlight_transmittance_mix);
		return transmit_sample;
	}
	return 1;
}
#endif  // LUTGEN

#ifdef SKY_SHADER
void DrawPhysicalSky(inout float4 color, PS_INPUT input)
{
	float3 cam_pos_km = float3(0, 0, PhysSkyBuffer[0].cam_height_km);
	float3 view_dir = normalize(input.WorldPosition.xyz);

	float2 transmit_uv = getHeightZenithLutUv(PhysSkyBuffer[0].cam_height_km, view_dir);
	float2 sky_lut_uv = cylinderMapAdjusted(view_dir);

	// Sky
#	if defined(DITHER) && !defined(TEX)
	color = float4(0, 0, 0, 1);

	bool is_sky = rayIntersectSphere(cam_pos_km, view_dir, PhysSkyBuffer[0].planet_radius) < 0;
	if (is_sky) {
		if (PhysSkyBuffer[0].override_vanilla_celestials) {
			float cos_sun_view = clamp(dot(PhysSkyBuffer[0].sun_dir, view_dir), -1, 1);
			float cos_masser_view = clamp(dot(PhysSkyBuffer[0].masser_dir, view_dir), -1, 1);
			float cos_secunda_view = clamp(dot(PhysSkyBuffer[0].secunda_dir, view_dir), -1, 1);

			bool is_sun = cos_sun_view > PhysSkyBuffer[0].sun_aperture_cos;
			bool is_masser = cos_masser_view > PhysSkyBuffer[0].masser_aperture_cos;
			bool is_secunda = cos_secunda_view > PhysSkyBuffer[0].secunda_aperture_cos;

			if (is_sun) {
				color.rgb = PhysSkyBuffer[0].sun_disc_color;

				float tan_sun_view = sqrt(1 - cos_sun_view * cos_sun_view) / cos_sun_view;
				float norm_dist = tan_sun_view * PhysSkyBuffer[0].sun_aperture_cos * PhysSkyBuffer[0].sun_aperture_rcp_sin;
				float3 darken_factor = limbDarkenHestroffer(norm_dist);
				color.rgb *= darken_factor;
			}
			if (is_masser) {
				float3 rightvec = cross(PhysSkyBuffer[0].masser_dir, PhysSkyBuffer[0].masser_upvec);
				float3 disp = normalize(view_dir - PhysSkyBuffer[0].masser_dir);
				float2 uv = normalize(float2(dot(rightvec, disp), dot(-PhysSkyBuffer[0].masser_upvec, disp)));
				uv *= sqrt(1 - cos_masser_view * cos_masser_view) * rsqrt(1 - PhysSkyBuffer[0].masser_aperture_cos * PhysSkyBuffer[0].masser_aperture_cos);  // todo: put it in cpu
				uv = uv * .5 + .5;

				float4 samp = TexMasser.Sample(SampBaseSampler, uv);
				color.rgb = lerp(color.rgb, samp.rgb * PhysSkyBuffer[0].masser_brightness, samp.w);
			}
			if (is_secunda) {
				float3 rightvec = cross(PhysSkyBuffer[0].secunda_dir, PhysSkyBuffer[0].secunda_upvec);
				float3 disp = normalize(view_dir - PhysSkyBuffer[0].secunda_dir);
				float2 uv = normalize(float2(dot(rightvec, disp), dot(-PhysSkyBuffer[0].secunda_upvec, disp)));
				uv *= sqrt(1 - cos_secunda_view * cos_secunda_view) * rsqrt(1 - PhysSkyBuffer[0].secunda_aperture_cos * PhysSkyBuffer[0].secunda_aperture_cos);
				uv = uv * .5 + .5;

				float4 samp = TexSecunda.Sample(SampBaseSampler, uv);
				color.rgb = lerp(color.rgb, samp.rgb * PhysSkyBuffer[0].secunda_brightness, samp.w);
			}
		}
	}

	if (any(color.rgb > 0))
		color.rgb *= TexTransmittance.SampleLevel(TransmittanceSampler, transmit_uv, 0).rgb;  // may cause nan? need investigation
	color.rgb += PhysSkyBuffer[0].dirlight_color * TexSkyView.SampleLevel(SkyViewSampler, sky_lut_uv, 0).rgb;
#	endif

	// Other vanilla meshes
#	if defined(MOONMASK)
	if (PhysSkyBuffer[0].override_vanilla_celestials)
		discard;
#	endif

#	if defined(TEX)
#		if defined(CLOUDS)  // cloud

	if (!PhysSkyBuffer[0].enable_vanilla_clouds)
		discard;
	if (PhysSkyBuffer[0].override_dirlight_color) {
		float3 dirLightColor = PhysSkyBuffer[0].dirlight_color;

		float cloud_dist = rayIntersectSphere(cam_pos_km, view_dir, PhysSkyBuffer[0].cam_height_km + PhysSkyBuffer[0].cloud_height);  // planetary
		float3 cloud_pos = cam_pos_km + cloud_dist * view_dir;
		// light transmit
		float2 lut_uv = getHeightZenithLutUv(cloud_pos, PhysSkyBuffer[0].dirlight_dir);
		{
			float3 transmit_sample = TexTransmittance.SampleLevel(TransmittanceSampler, lut_uv, 0).rgb;
			dirLightColor *= transmit_sample;
		}

		// manual adjustment
		dirLightColor = lerp(dot(dirLightColor, float3(0.2125, 0.7154, 0.0721)), dirLightColor, PhysSkyBuffer[0].cloud_saturation) * PhysSkyBuffer[0].cloud_mult;

		// hitting the cloud
		float3 cloud_color = color.rgb;
		color.rgb *= dirLightColor;

		// phase
		float vdl = dot(view_dir, PhysSkyBuffer[0].dirlight_dir);
		// float vdu = dot(view_dir, cloud_pos);
		// float ldu = dot(PhysSkyBuffer[0].dirlight_dir, cloud_pos);

		// thicker cloud = more multiscatter
		// brighter cloud = artist said it refracts more so that it is
		float scatter_factor = saturate(lerp(1, PhysSkyBuffer[0].cloud_alpha_heuristics, color.w)) *
		                       saturate(lerp(PhysSkyBuffer[0].cloud_color_heuristics, 1, dot(cloud_color, float3(0.2125, 0.7154, 0.0721))));
		float scatter_strength = 4 * PI * scatter_factor *
		                         ((miePhaseHenyeyGreensteinDualLobe(vdl, PhysSkyBuffer[0].cloud_phase_g0, -PhysSkyBuffer[0].cloud_phase_g1, PhysSkyBuffer[0].cloud_phase_w) +
									 miePhaseHenyeyGreensteinDualLobe(vdl * .5, PhysSkyBuffer[0].cloud_phase_g0, -PhysSkyBuffer[0].cloud_phase_g1, PhysSkyBuffer[0].cloud_phase_w) * scatter_factor));
		float3 multiscatter_value = 0.f;
		if (PhysSkyBuffer[0].cloud_atmos_scatter)
			multiscatter_value = TexMultiScatter.SampleLevel(TransmittanceSampler, lut_uv, 0).rgb * dirLightColor * PhysSkyBuffer[0].cloud_atmos_scatter;
		color.rgb = (color.rgb + multiscatter_value) * scatter_strength;

		// ap
		if (PhysSkyBuffer[0].enable_aerial) {
			uint3 ap_dims;
			TexAerialPerspective.GetDimensions(ap_dims.x, ap_dims.y, ap_dims.z);

			float depth_slice = lerp(.5 / ap_dims.z, 1 - .5 / ap_dims.z, saturate(cloud_dist / PhysSkyBuffer[0].aerial_perspective_max_dist));
			float4 ap_sample = TexAerialPerspective.SampleLevel(SkyViewSampler, float3(cylinderMapAdjusted(view_dir), depth_slice), 0);
			ap_sample.rgb *= PhysSkyBuffer[0].dirlight_color;

			color.rgb = color.rgb * ap_sample.w + ap_sample.rgb;
		}
	}

#		elif defined(DITHER)  //  glare

	if (PhysSkyBuffer[0].override_vanilla_celestials)
		discard;

#		else  // Texture

	uint obj_type = SkyPerGeometryBuffer[0].sky_object_type;

	if (obj_type != 0)
		if (PhysSkyBuffer[0].override_vanilla_celestials)
			discard;

			// if (obj_type == 2 || obj_type == 3) {  // moons
			// 	float mult = obj_type == 2 ? PhysSkyBuffer[0].masser_brightness : PhysSkyBuffer[0].secunda_brightness;
			// 	color.rgb *= mult * TexTransmittance.Sample(SkyViewSampler, transmit_uv).rgb;
			// 	// color.rgb += PhysSkyBuffer[0].dirlight_color * TexSkyView.Sample(SkyViewSampler, sky_lut_uv); // blending mode does it
			// }

#		endif
#	endif
}
#endif  // SKY_SHADER
