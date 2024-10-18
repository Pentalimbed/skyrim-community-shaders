#include "../PhysicalSky.h"

#include "PSCommon.h"

#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Orbit, azimuth, zenith, drift)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Trajectory,
	minima, maxima,
	period_orbital, offset_orbital,
	period_long, offset_long)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PhysicalSky::Settings,
	enable_sky,
	enable_aerial,
	transmittance_step,
	multiscatter_step,
	multiscatter_sqrt_samples,
	skyview_step,
	aerial_perspective_max_dist,
	unit_scale,
	bottom_z,
	planet_radius,
	atmos_thickness,
	ground_albedo,
	enable_vanilla_clouds,
	cloud_height,
	cloud_saturation,
	cloud_mult,
	cloud_phase_g0,
	cloud_phase_g1,
	cloud_phase_w,
	cloud_alpha_heuristics,
	cloud_color_heuristics,
	override_dirlight_color,
	override_dirlight_dir,
	moonlight_follows_secunda,
	dirlight_transmittance_mix,
	treelod_saturation,
	treelod_mult,
	sunlight_color,
	moonlight_color,
	phase_dep_moonlight,
	masser_moonlight_min,
	masser_moonlight_color,
	secunda_moonlight_min,
	secunda_moonlight_color,
	light_transition_angles,
	sun_disc_color,
	sun_angular_radius,
	override_sun_traj,
	sun_trajectory,
	override_masser_traj,
	masser_trajectory,
	masser_angular_radius,
	masser_brightness,
	override_secunda_traj,
	secunda_trajectory,
	secunda_angular_radius,
	secunda_brightness,
	stars_brightness,
	ap_inscatter_mix,
	ap_transmittance_mix,
	rayleigh_scatter,
	rayleigh_absorption,
	rayleigh_decay,
	aerosol_phase_func_g,
	aerosol_scatter,
	aerosol_absorption,
	aerosol_decay,
	ozone_absorption,
	ozone_altitude,
	ozone_thickness)

RE::NiPoint3 Orbit::getDir(float t)
{
	float t_rad = t * 2 * RE::NI_PI;

	RE::NiPoint3 result = { sin(t_rad) * cos(drift), cos(t_rad) * cos(drift), sin(drift) };

	RE::NiMatrix3 rotmat = RE::NiMatrix3(0, 0, azimuth) * RE::NiMatrix3(zenith + .5f * RE::NI_PI, 0, 0);
	result = rotmat * result;

	result.Unitize();

	return result;
}

RE::NiPoint3 Orbit::getTangent(float t)
{
	float t_rad = t * 2 * RE::NI_PI;

	RE::NiPoint3 result = { cos(t_rad), 0, sin(t_rad) };

	RE::NiMatrix3 rotmat;
	rotmat.SetEulerAnglesXYZ(zenith, 0, azimuth);
	result = rotmat * result;

	return result;
}

Orbit Trajectory::getMixedOrbit(float gameDaysPassed)
{
	auto lerp = sin((gameDaysPassed + offset_long) / period_long * 2 * RE::NI_PI) * .5f + .5f;
	Orbit orbit = {
		.azimuth = std::lerp(minima.azimuth, maxima.azimuth, lerp),
		.zenith = std::lerp(minima.zenith, maxima.zenith, lerp),
		.drift = std::lerp(minima.drift, maxima.drift, lerp)
	};
	return orbit;
}

RE::NiPoint3 Trajectory::getDir(float gameDaysPassed)
{
	auto t = (gameDaysPassed + offset_orbital) / period_orbital;
	return getMixedOrbit(gameDaysPassed).getDir(t);
}

RE::NiPoint3 Trajectory::getTangent(float gameDaysPassed)
{
	auto t = (gameDaysPassed + offset_orbital) / period_orbital;
	return getMixedOrbit(gameDaysPassed).getTangent(t);
}

void PhysicalSky::LoadSettings(json& o_json)
{
	settings = o_json;
}

void PhysicalSky::SaveSettings(json& o_json)
{
	o_json = settings;
}

void PhysicalSky::UpdateBuffer()
{
	float sun_aperture_cos = cos(settings.sun_angular_radius);
	float sun_aperture_rcp_sin = 1.f / sqrt(1 - sun_aperture_cos * sun_aperture_cos);  // I trust u compiler

	phys_sky_sb_data = {
		.enable_sky = settings.enable_sky && NeedLutsUpdate(),
		.enable_aerial = settings.enable_aerial,
		.transmittance_step = settings.transmittance_step,
		.multiscatter_step = settings.multiscatter_step,
		.multiscatter_sqrt_samples = settings.multiscatter_sqrt_samples,
		.skyview_step = settings.skyview_step,
		.aerial_perspective_max_dist = settings.aerial_perspective_max_dist,
		.unit_scale = settings.unit_scale,
		.bottom_z = settings.bottom_z,
		.planet_radius = settings.planet_radius,
		.atmos_thickness = settings.atmos_thickness,
		.ground_albedo = settings.ground_albedo,
		.override_dirlight_color = settings.override_dirlight_color,
		.dirlight_transmittance_mix = settings.override_dirlight_color ? 1.f : settings.dirlight_transmittance_mix,
		.treelod_saturation = settings.treelod_saturation,
		.treelod_mult = settings.treelod_mult,
		.enable_vanilla_clouds = settings.enable_vanilla_clouds,
		.cloud_height = settings.cloud_height,
		.cloud_saturation = settings.cloud_saturation,
		.cloud_mult = settings.cloud_mult,
		.cloud_atmos_scatter = settings.cloud_atmos_scatter,
		.cloud_phase_g0 = settings.cloud_phase_g0,
		.cloud_phase_g1 = settings.cloud_phase_g1,
		.cloud_phase_w = settings.cloud_phase_w,
		.cloud_alpha_heuristics = settings.cloud_alpha_heuristics,
		.cloud_color_heuristics = settings.cloud_color_heuristics,
		.sun_disc_color = settings.sun_disc_color,
		.sun_aperture_cos = sun_aperture_cos,
		.sun_aperture_rcp_sin = sun_aperture_rcp_sin,
		.masser_aperture_cos = cos(settings.masser_angular_radius),
		.masser_brightness = settings.masser_brightness,
		.secunda_aperture_cos = cos(settings.secunda_angular_radius),
		.secunda_brightness = settings.secunda_brightness,
		.ap_inscatter_mix = settings.ap_inscatter_mix,
		.ap_transmittance_mix = settings.ap_transmittance_mix,
		.rayleigh_scatter = settings.rayleigh_scatter * 1e-3f,  // km^-1
		.rayleigh_absorption = settings.rayleigh_absorption * 1e-3f,
		.rayleigh_decay = settings.rayleigh_decay,
		.aerosol_phase_func_g = settings.aerosol_phase_func_g,
		.aerosol_scatter = settings.aerosol_scatter * 1e-3f,
		.aerosol_absorption = settings.aerosol_absorption * 1e-3f,
		.aerosol_decay = settings.aerosol_decay,
		.ozone_absorption = settings.ozone_absorption * 1e-3f,
		.ozone_altitude = settings.ozone_altitude,
		.ozone_thickness = settings.ozone_thickness
	};

	// DYNAMIC STUFF
	if (phys_sky_sb_data.enable_sky)
		UpdateOrbitsAndHeight();

	phys_sky_sb->Update(&phys_sky_sb_data, sizeof(phys_sky_sb_data));
}

void PhysicalSky::UpdateOrbitsAndHeight()
{
	// height part
	RE::NiPoint3 cam_pos = { 0, 0, 0 };
	if (auto cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot) {
		cam_pos = cam->cameraRoot->world.translate;
		phys_sky_sb_data.cam_height_km = (cam_pos.z - settings.bottom_z) * settings.unit_scale * 1.428e-5f + settings.planet_radius;
	}

	// orbits
	auto sky = RE::Sky::GetSingleton();
	// auto sun = sky->sun;
	auto masser = sky->masser;
	auto secunda = sky->secunda;
	// auto stars = sky->stars;

	RE::NiPoint3 sun_dir;
	auto calendar = RE::Calendar::GetSingleton();
	if (calendar) {
		float game_days = getDayInYear();  // Last Seed = 8
		if (settings.override_sun_traj) {
			sun_dir = settings.sun_trajectory.getDir(game_days);
		} else {
			sun_dir = Orbit{}.getDir(getVanillaSunLerpFactor());
		}

		if (settings.override_masser_traj) {
			auto masser_dir = settings.masser_trajectory.getDir(game_days);
			auto masser_up = masser_dir.Cross(settings.masser_trajectory.getTangent(game_days));

			phys_sky_sb_data.masser_dir = { masser_dir.x, masser_dir.y, masser_dir.z };
			phys_sky_sb_data.masser_upvec = { masser_up.x, masser_up.y, masser_up.z };
		} else if (masser) {
			auto masser_dir = masser->moonMesh->world.translate - cam_pos;
			masser_dir.Unitize();
			auto masser_upvec = masser->moonMesh->world.rotate * RE::NiPoint3{ 0, 1, 0 };

			phys_sky_sb_data.masser_dir = { masser_dir.x, masser_dir.y, masser_dir.z };
			phys_sky_sb_data.masser_upvec = { masser_upvec.x, masser_upvec.y, masser_upvec.z };
		}

		if (settings.override_secunda_traj) {
			auto secunda_dir = settings.secunda_trajectory.getDir(game_days);
			auto secunda_up = secunda_dir.Cross(settings.secunda_trajectory.getTangent(game_days));

			phys_sky_sb_data.secunda_dir = { secunda_dir.x, secunda_dir.y, secunda_dir.z };
			phys_sky_sb_data.secunda_upvec = { secunda_up.x, secunda_up.y, secunda_up.z };
		} else if (secunda) {
			auto secunda_dir = secunda->moonMesh->world.translate - cam_pos;
			secunda_dir.Unitize();
			auto secunda_upvec = secunda->moonMesh->world.rotate * RE::NiPoint3{ 0, 1, 0 };

			phys_sky_sb_data.secunda_dir = { secunda_dir.x, secunda_dir.y, secunda_dir.z };
			phys_sky_sb_data.secunda_upvec = { secunda_upvec.x, secunda_upvec.y, secunda_upvec.z };
		}
	}
	phys_sky_sb_data.sun_dir = { sun_dir.x, sun_dir.y, sun_dir.z };

	// sun or moon
	float sun_dir_angle = asin(sun_dir.z);
	float sun_moon_transition = (sun_dir_angle - settings.light_transition_angles.x) / (settings.light_transition_angles.y - settings.light_transition_angles.x);
	if (sun_moon_transition < .5) {
		phys_sky_sb_data.dirlight_color = (1.f - std::clamp(sun_moon_transition * 2.f, 0.f, 1.f)) * settings.sunlight_color;
		phys_sky_sb_data.dirlight_dir = phys_sky_sb_data.sun_dir;
	} else {  // moon
		float3 moonlight = settings.moonlight_color;
		if (settings.phase_dep_moonlight) {
			float masser_phase = current_moon_phases[0] > 4 ? (current_moon_phases[0] - 4) * .25f : current_moon_phases[0] * .25f;
			float3 masser_light = (1 - masser_phase * (1 - settings.masser_moonlight_min)) * settings.masser_moonlight_color;
			float secunda_phase = current_moon_phases[1] > 4 ? (current_moon_phases[1] - 4) * .25f : current_moon_phases[1] * .25f;
			float3 secunda_light = (1 - secunda_phase * (1 - settings.secunda_moonlight_min)) * settings.secunda_moonlight_color;
			moonlight = masser_light + secunda_light;
		}
		phys_sky_sb_data.dirlight_color = std::clamp(sun_moon_transition * 2.f - 1, 0.f, 1.f) * moonlight;

		phys_sky_sb_data.dirlight_dir = settings.moonlight_follows_secunda ? phys_sky_sb_data.secunda_dir : phys_sky_sb_data.masser_dir;
	}

	float sin_sun_horizon = phys_sky_sb_data.dirlight_dir.z;
	float norm_dist = sin_sun_horizon * phys_sky_sb_data.sun_aperture_rcp_sin;
	if (abs(norm_dist) < 1)
		phys_sky_sb_data.horizon_penumbra = .5f + norm_dist * .5f;  // horizon penumbra, not accurate but good enough
	else
		phys_sky_sb_data.horizon_penumbra = sin_sun_horizon > 0;
}

void PhysicalSky::Hooks::NiPoint3_Normalize::thunk(RE::NiPoint3* a_this)
{
	auto feat = GetSingleton();
	if (feat->phys_sky_sb_data.enable_sky && feat->settings.override_dirlight_dir) {
		float3 mod_dirlight_dir = -feat->phys_sky_sb_data.dirlight_dir;
		*a_this = { mod_dirlight_dir.x, mod_dirlight_dir.y, mod_dirlight_dir.z };
	} else
		func(a_this);
}
