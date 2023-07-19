#include "../PhysicalWeather.h"
#include "PhysicalWeather_Common.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Orbit, azimuth, zenith, offset)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Trajectory,
	minima,
	maxima,
	period_dirunal,
	offset_dirunal,
	period_shift,
	offset_shift)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PhysicalWeather::Settings,
	enable_sky,
	enable_scatter,
	enable_tonemap,
	tonemap_keyval,
	transmittance_step,
	multiscatter_step,
	multiscatter_sqrt_samples,
	skyview_step,
	aerial_perspective_max_dist,
	unit_scale,
	bottom_z,
	ground_radius,
	atmos_thickness,
	ground_albedo,
	sunlight_color,
	moonlight_color,
	critcial_sun_angle,
	sun_trajectory,
	limb_darken_model,
	limb_darken_power,
	sun_color,
	sun_aperture_angle,
	masser_aperture_angle,
	masser_brightness,
	secunda_aperture_angle,
	secunda_brightness,
	rayleigh_scatter,
	rayleigh_absorption,
	rayleigh_decay,
	mie_phase_func,
	mie_g0,
	mie_g1,
	mie_w,
	mie_d,
	mie_absorption,
	mie_decay,
	ozone_absorption,
	ozone_height,
	ozone_thickness,
	ap_inscatter_mix,
	ap_transmittance_mix,
	light_transmittance_mix,
	cloud_bottom_height)

RE::NiPoint3 Orbit::getDir(float t)
{
	float t_rad = t * 2 * RE::NI_PI;

	float orbit_r = sqrt(1 - offset * offset);
	RE::NiPoint3 result = { sin(t_rad) * orbit_r, offset, -cos(t_rad) * orbit_r };

	RE::NiMatrix3 rotmat;
	rotmat.SetEulerAnglesXYZ(zenith, 0, azimuth);
	result = rotmat * result;

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
	auto lerp = sin((gameDaysPassed + offset_shift) / period_shift * 2 * RE::NI_PI) * .5f + .5f;
	Orbit orbit = {
		.azimuth = std::lerp(minima.azimuth, maxima.azimuth, lerp),
		.zenith = std::lerp(minima.zenith, maxima.zenith, lerp),
		.offset = std::lerp(minima.offset, maxima.offset, lerp)
	};
	return orbit;
}

RE::NiPoint3 Trajectory::getDir(float gameDaysPassed)
{
	auto t = (gameDaysPassed + offset_dirunal) / period_dirunal;
	return getMixedOrbit(gameDaysPassed).getDir(t);
}

RE::NiPoint3 Trajectory::getTangent(float gameDaysPassed)
{
	auto t = (gameDaysPassed + offset_dirunal) / period_dirunal;
	return getMixedOrbit(gameDaysPassed).getTangent(t);
}

void PhysicalWeather::Load(json& o_json)
{
	if (o_json[GetName()].is_object())
		settings = o_json[GetName()];

	Feature::Load(o_json);
}

void PhysicalWeather::Save(json& o_json)
{
	o_json[GetName()] = settings;
}

void PhysicalWeather::Update()
{
	static FrameChecker frame_checker;
	if (!frame_checker.isNewFrame())
		return;

	phys_weather_sb_content = {
		.enable_sky = settings.enable_sky && (RE::Sky::GetSingleton()->mode.get() == RE::Sky::Mode::kFull),
		.enable_scatter = settings.enable_scatter,
		.enable_tonemap = settings.enable_tonemap,
		.tonemap_keyval = settings.tonemap_keyval,

		.transmittance_step = settings.transmittance_step,
		.multiscatter_step = settings.multiscatter_step,
		.multiscatter_sqrt_samples = settings.multiscatter_sqrt_samples,
		.skyview_step = settings.skyview_step,
		.aerial_perspective_max_dist = settings.aerial_perspective_max_dist,

		.cloud_march_step = settings.cloud_march_step,
		.cloud_self_shadow_step = settings.cloud_self_shadow_step,

		.unit_scale = settings.unit_scale,
		.bottom_z = settings.bottom_z,
		.ground_radius = settings.ground_radius,
		.atmos_thickness = settings.atmos_thickness,
		.ground_albedo = settings.ground_albedo,

		.limb_darken_model = static_cast<uint32_t>(settings.limb_darken_model),
		.limb_darken_power = settings.limb_darken_power,
		.sun_color = settings.sun_color,
		.sun_aperture_cos = cos(settings.sun_aperture_angle),

		.masser_aperture_cos = cos(settings.masser_aperture_angle),
		.masser_brightness = settings.masser_brightness,

		.secunda_aperture_cos = cos(settings.secunda_aperture_angle),
		.secunda_brightness = settings.secunda_brightness,

		.stars_brightness = settings.stars_brightness,

		.rayleigh_scatter = settings.rayleigh_scatter,
		.rayleigh_absorption = settings.rayleigh_absorption,
		.rayleigh_decay = settings.rayleigh_decay,

		.mie_phase_func = static_cast<uint32_t>(settings.mie_phase_func),
		.mie_g0 = settings.mie_g0,
		.mie_g1 = settings.mie_g1,
		.mie_w = settings.mie_w,
		.mie_d = settings.mie_d,
		.mie_scatter = settings.mie_scatter,
		.mie_absorption = settings.mie_absorption,
		.mie_decay = settings.mie_decay,

		.ozone_absorption = settings.ozone_absorption,
		.ozone_height = settings.ozone_height,
		.ozone_thickness = settings.ozone_thickness,

		.ap_inscatter_mix = settings.ap_inscatter_mix,
		.ap_transmittance_mix = settings.ap_transmittance_mix,
		.light_transmittance_mix = settings.light_transmittance_mix,

		.cloud_bottom_height = settings.cloud_bottom_height,
		.cloud_upper_height = settings.cloud_upper_height,
		.cloud_noise_freq = settings.cloud_noise_freq,
		.cloud_scatter = settings.cloud_scatter,
		.cloud_absorption = settings.cloud_absorption,
	};

	// dynamic variables
	static uint32_t custom_timer = 0;
	custom_timer += uint32_t(RE::GetSecondsSinceLastFrame() * 1e3f);
	phys_weather_sb_content.timer = custom_timer * 1e-3f;

	auto accumulator = RE::BSGraphics::BSShaderAccumulator::GetCurrentAccumulator();
	auto dir_light = skyrim_cast<RE::NiDirectionalLight*>(accumulator->GetRuntimeData().activeShadowSceneNode->GetRuntimeData().sunLight->light.get());
	if (dir_light) {
		auto sun_dir = -dir_light->GetWorldDirection();
		phys_weather_sb_content.sun_dir = { sun_dir.x, sun_dir.y, sun_dir.z };
	}

	RE::NiPoint3 cam_pos = { 0, 0, 0 };
	if (auto cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot) {
		cam_pos = cam->cameraRoot->world.translate;
		phys_weather_sb_content.player_cam_pos = { cam_pos.x, cam_pos.y, cam_pos.z };
	}

	auto sky = RE::Sky::GetSingleton();
	// auto sun = sky->sun;
	auto masser = sky->masser;
	auto secunda = sky->secunda;
	// auto stars = sky->stars;
	// if (sun) {
	// 	RE::NiPoint3 rise_dir = { 0.995, 0.1, 0 };
	// 	auto sun_dir = sun->sunBase->world.translate - cam_pos;
	// 	sun_dir.Unitize();
	// 	if (abs(sun->light->GetWorldDirection().z) < 1e-10)            // rise / set
	// 		sun_dir = rise_dir * 2 * rise_dir.Dot(sun_dir) - sun_dir;  // flip
	// 	phys_weather_sb_content.sun_dir = { sun_dir.x, sun_dir.y, sun_dir.z };
	// }  // during night it flips
	if (masser) {
		auto masser_dir = masser->moonMesh->world.translate - cam_pos;
		masser_dir.Unitize();
		auto masser_upvec = masser->moonMesh->world.rotate * RE::NiPoint3{ 0, 1, 0 };

		phys_weather_sb_content.masser_dir = { masser_dir.x, masser_dir.y, masser_dir.z };
		phys_weather_sb_content.masser_upvec = { masser_upvec.x, masser_upvec.y, masser_upvec.z };
	}
	if (secunda) {
		auto secunda_dir = secunda->moonMesh->world.translate - cam_pos;
		secunda_dir.Unitize();
		auto secunda_upvec = secunda->moonMesh->world.rotate * RE::NiPoint3{ 0, 1, 0 };

		phys_weather_sb_content.secunda_dir = { secunda_dir.x, secunda_dir.y, secunda_dir.z };
		phys_weather_sb_content.secunda_upvec = { secunda_upvec.x, secunda_upvec.y, secunda_upvec.z };
	}

	UpdateOrbits();

	UploadPhysWeatherSB();
}

void PhysicalWeather::UpdateOrbits()
{
	auto calendar = RE::Calendar::GetSingleton();
	if (calendar) {
		auto game_time = calendar->GetCurrentGameTime();

		auto sun_dir = settings.sun_trajectory.getDir(game_time);
		phys_weather_sb_content.sun_dir = { sun_dir.x, sun_dir.y, sun_dir.z };

		auto masser_dir = settings.masser_trajectory.getDir(game_time);
		auto masser_up = masser_dir.Cross(settings.masser_trajectory.getTangent(game_time));
		phys_weather_sb_content.masser_dir = { masser_dir.x, masser_dir.y, masser_dir.z };
		phys_weather_sb_content.masser_upvec = { masser_up.x, masser_up.y, masser_up.z };

		auto secunda_dir = settings.secunda_trajectory.getDir(game_time);
		auto secunda_up = secunda_dir.Cross(settings.secunda_trajectory.getTangent(game_time));
		phys_weather_sb_content.secunda_dir = { secunda_dir.x, secunda_dir.y, secunda_dir.z };
		phys_weather_sb_content.secunda_upvec = { secunda_up.x, secunda_up.y, secunda_up.z };
	}

	// sun or moon
	if (phys_weather_sb_content.sun_dir.z > -sin(settings.critcial_sun_angle)) {
		phys_weather_sb_content.dirlight_color = settings.sunlight_color;
		phys_weather_sb_content.dirlight_dir = phys_weather_sb_content.sun_dir;
	} else {
		phys_weather_sb_content.dirlight_color = settings.moonlight_color;
		phys_weather_sb_content.dirlight_dir = phys_weather_sb_content.masser_dir;
	}
}