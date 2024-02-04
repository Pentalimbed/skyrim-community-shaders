#include "../PhysicalSky.h"

#include "PSCommon.h"

#include "Util.h"

void OrbitEdit(Orbit& orbit)
{
	ImGui::SliderAngle("Azimuth", &orbit.azimuth, -180, 180, "%.1f deg");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("Clockwise orientation of where sun rise/set. 0 is east-west.");
	ImGui::SliderAngle("Zenith", &orbit.zenith, -90, 90, "%.1f deg");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(
			"Inclination of the orbit. At 0 azimuth, positive values tilt the midday sun northward.\n"
			"On an earth-like planet, this is the latitude, positive in the south, negative in the north.");
	ImGui::SliderAngle("Drift", &orbit.drift, -90, 90, "%.1f deg");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text(
			"Offset of the whole orbit. At 0 azimuth, positive values move the orbit northward.\n"
			"On an earth-like planet, this is the tilt of its axis, positive in summer, negative in winter.");
}

void TrajectoryEdit(Trajectory& traj)
{
	if (ImGui::TreeNodeEx("Orbit A (Winter)", ImGuiTreeNodeFlags_DefaultOpen)) {
		OrbitEdit(traj.minima);
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("Orbit B (Summer)", ImGuiTreeNodeFlags_DefaultOpen)) {
		OrbitEdit(traj.maxima);
		ImGui::TreePop();
	}

	ImGui::InputFloat("Orbital Period", &traj.period_orbital, 0, 0, "%.3f day(s)");
	traj.period_orbital = std::max(1e-6f, traj.period_orbital);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("The time it takes to go one circle.");
	ImGui::SliderFloat("Orbital Period Offset", &traj.offset_orbital, -traj.period_orbital, traj.period_orbital, "%.3f day(s)");

	ImGui::InputFloat("Drift Period", &traj.period_long), 0, 0, "%.1f day(s)";
	traj.period_long = std::max(1e-6f, traj.period_long);
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("The time it takes for the orbit to drift from A to B and back.");
	ImGui::SliderFloat("Drift Period Offset", &traj.offset_long, -traj.period_long, traj.period_long, "%.1f day(s)");
}

void PhysicalSky::DrawSettings()
{
	if (ImGui::BeginTabBar("##PHYSSKY")) {
		if (ImGui::BeginTabItem("General")) {
			SettingsGeneral();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("World")) {
			SettingsWorld();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Lighting")) {
			SettingsLighting();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Clouds")) {
			SettingsClouds();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Celestials")) {
			SettingsCelestials();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Atmosphere")) {
			SettingsAtmosphere();
			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Debug")) {
			SettingsDebug();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
}

void PhysicalSky::SettingsGeneral()
{
	if (!CheckComputeShaders())
		ImGui::TextColored({ 1, .1, .1, 1 }, "Shader compilation failed!");

	ImGui::Checkbox("Enable Physcial Sky", &settings.enable_sky);

	ImGui::SeparatorText("Aerial Perspective");
	{
		ImGui::Checkbox("Enable Aerial Perspective", &settings.enable_aerial);

		ImGui::SliderFloat("In-scatter", &settings.ap_inscatter_mix, 0.f, 2.f);
		ImGui::SliderFloat("View Transmittance", &settings.ap_transmittance_mix, 0.f, 1.f);
	}

	ImGui::SeparatorText("Performance");
	{
		ImGui::DragScalar("Transmittance Steps", ImGuiDataType_U32, &settings.transmittance_step);
		ImGui::DragScalar("Multiscatter Steps", ImGuiDataType_U32, &settings.multiscatter_step);
		ImGui::DragScalar("Multiscatter Sqrt Samples", ImGuiDataType_U32, &settings.multiscatter_sqrt_samples);
		ImGui::DragScalar("Sky View Steps", ImGuiDataType_U32, &settings.skyview_step);
		ImGui::SliderFloat("Aerial Perspective Max Dist", &settings.aerial_perspective_max_dist, 0, settings.atmos_thickness, "%.3f km");
	}
}
void PhysicalSky::SettingsWorld()
{
	ImGui::TextWrapped("The planetary properties of Nirn, or the nearest macroscopic part of it.");

	ImGui::SeparatorText("Scale");
	{
		ImGui::SliderFloat("Unit Scale", &settings.unit_scale, 0.1f, 20.f);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(
				"Relative scale of the game length unit compared to real physical ones, used by atmosphere rendering and others.\n"
				"Greater unit scale renders far landscape less clear and with more aerial perspective, also makes heights higher.");
		ImGui::InputFloat("Bottom Z", &settings.bottom_z, 0, 0, "%.3f game unit");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(
				"The lowest elevation of the worldspace you shall reach. "
				"You can check it by standing at sea level and using \"getpos z\" console command.");

		ImGui::SliderFloat("Planet Radius", &settings.planet_radius, 0.f, 1e4f, "%.1f km");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("The supposed radius of the planet Nirn, or whatever rock you are on.");
		ImGui::SliderFloat("Atmosphere Thickness", &settings.atmos_thickness, 0.f, 200.f, "%.1f km");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("The thickness of atmosphere that contributes to lighting.");
	}

	ImGui::SeparatorText("Misc");

	ImGui::ColorEdit3("Ground Albedo", &settings.ground_albedo.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_DisplayHSV);
	if (auto _tt = Util::HoverTooltipWrapper()) {
		ImGui::Text("How much light gets reflected from the ground far away.");
		ImGui::Text("A neat chart from wiki and other sources:");
		// http://www.climatedata.info/forcing/albedo/
		if (ImGui::BeginTable("Albedo Table", 2)) {
			ImGui::TableNextColumn();
			ImGui::Text("Open ocean");
			ImGui::TableNextColumn();
			ImGui::Text("0.06");

			ImGui::TableNextColumn();
			ImGui::Text("Conifer forest, summer");
			ImGui::TableNextColumn();
			ImGui::Text("0.08 to 0.15");

			ImGui::TableNextColumn();
			ImGui::Text("Deciduous forest");
			ImGui::TableNextColumn();
			ImGui::Text("0.15 to 0.18");

			ImGui::TableNextColumn();
			ImGui::Text("Bare soil");
			ImGui::TableNextColumn();
			ImGui::Text("0.17");

			ImGui::TableNextColumn();
			ImGui::Text("Tundra");
			ImGui::TableNextColumn();
			ImGui::Text("0.20");

			ImGui::TableNextColumn();
			ImGui::Text("Green grass");
			ImGui::TableNextColumn();
			ImGui::Text("0.25");

			ImGui::TableNextColumn();
			ImGui::Text("Desert sand");
			ImGui::TableNextColumn();
			ImGui::Text("0.40");

			ImGui::TableNextColumn();
			ImGui::Text("Old/melting snow");
			ImGui::TableNextColumn();
			ImGui::Text("0.40 to 0.80");

			ImGui::TableNextColumn();
			ImGui::Text("Ocean ice");
			ImGui::TableNextColumn();
			ImGui::Text("0.50 to 0.70");

			ImGui::TableNextColumn();
			ImGui::Text("Fresh snow");
			ImGui::TableNextColumn();
			ImGui::Text("0.80");

			ImGui::EndTable();
		}
	}
}

void PhysicalSky::SettingsLighting()
{
	ImGui::TextWrapped("How the sky is lit, as well as everything under the sun (or moon).");

	ImGui::SeparatorText("Syncing");
	{
		ImGui::Checkbox("Override Light Direction", &settings.override_dirlight_dir);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(
				"Directional light will use directions specified by trajectories in Celestials tab.\n"
				"Needs game running to update.");
		ImGui::Indent();
		ImGui::Checkbox("Moonlight Follows Secunda", &settings.moonlight_follows_secunda);
		ImGui::Unindent();

		ImGui::Checkbox("Override Light Color", &settings.override_dirlight_color);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(
				"The physical sky is lit differently from vanilla objects, because it needs values from outer space and is much brighter. "
				"With this on, everything will be lit by colors specified below.");
	}

	ImGui::SeparatorText("Sky");
	{
		ImGui::ColorEdit3("Sunlight Color", &settings.sunlight_color.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);

		ImGui::BeginDisabled(settings.phase_dep_moonlight);
		ImGui::ColorEdit3("Moonlight Color", &settings.moonlight_color.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		ImGui::EndDisabled();

		ImGui::Checkbox("Moon Phase Affects Moonlight", &settings.phase_dep_moonlight);
		ImGui::BeginDisabled(!settings.phase_dep_moonlight);
		ImGui::Indent();
		{
			ImGui::ColorEdit3("Masser", &settings.masser_moonlight_color.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
			ImGui::SliderFloat("Masser Min Brightness", &settings.masser_moonlight_min, 0.f, 1.f, "%.2f");
			ImGui::ColorEdit3("Secunda", &settings.secunda_moonlight_color.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
			ImGui::SliderFloat("Secunda Min Brightness", &settings.secunda_moonlight_min, 0.f, 1.f, "%.2f");
		}
		ImGui::Unindent();
		ImGui::EndDisabled();

		ImGui::SliderAngle("Sun/Moon Transition Start", &settings.light_transition_angles.x, -30, 0);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("When the sun dips this much below the horizon, the sky will gradually transition to being lit by moonlight.");
		ImGui::SliderAngle("Sun/Moon Transition End", &settings.light_transition_angles.y, -30, 0);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("When the sun dips this much below the horizon, the sky will completely transition to being lit by moonlight.");
	}

	ImGui::SeparatorText("Misc");
	{
		ImGui::BeginDisabled(settings.override_dirlight_color);
		{
			ImGui::SliderFloat("Light Transmittance Mix", &settings.dirlight_transmittance_mix, 0.f, 1.f, "%.2f");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Applying the filtering effect of light going through the atmosphere.");
		}
		ImGui::EndDisabled();

		ImGui::BeginDisabled(!settings.override_dirlight_color);
		{
			ImGui::SliderFloat("Tree LOD Saturation", &settings.treelod_saturation, 0.f, 1.f, "%.2f");
			ImGui::SliderFloat("Tree LOD Brightness", &settings.treelod_mult, 0.f, 1.f, "%.2f");
		}
		ImGui::EndDisabled();
	}
}

void PhysicalSky::SettingsClouds()
{
	ImGui::TextWrapped("Little fluffy clouds.");

	ImGui::SeparatorText("Vanilla Clouds");
	{
		ImGui::Checkbox("Enable Vanilla Clouds", &settings.enable_vanilla_clouds);

		if (!(settings.enable_vanilla_clouds && settings.override_dirlight_color)) {
			ImGui::TextDisabled("Below options require Override Light Color.");
			ImGui::BeginDisabled();
		}

		ImGui::SliderFloat("Height", &settings.cloud_height, 0.f, 20.f, "%.2f km");
		ImGui::SliderFloat("Saturation", &settings.cloud_saturation, 0.f, 1.f, "%.2f");
		ImGui::SliderFloat("Brightness", &settings.cloud_mult, 0.f, 2.f, "%.2f");
		ImGui::SliderFloat("Atmosphere Scattering", &settings.cloud_atmos_scatter, 0.f, 5.f, "%.2f");

		if (ImGui::TreeNodeEx("Phase Function", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderFloat("Forward Asymmetry", &settings.cloud_phase_g0, 0, 1, "%.2f");
			ImGui::SliderFloat("Backward Asymmetry", &settings.cloud_phase_g1, -1, 0, "%.2f");
			ImGui::SliderFloat("Backward Weight", &settings.cloud_phase_w, 0, 1, "%.2f");
			ImGui::TreePop();
		}

		if (ImGui::TreeNodeEx("Scattering Heuristics", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderFloat("Alpha", &settings.cloud_alpha_heuristics, -.5, 1, "%.2f");
			ImGui::SliderFloat("Value", &settings.cloud_color_heuristics, -.5, 1, "%.2f");
			ImGui::TreePop();
		}

		if (!(settings.enable_vanilla_clouds && settings.override_dirlight_color))
			ImGui::EndDisabled();
	}
}

void PhysicalSky::SettingsCelestials()
{
	ImGui::TextWrapped("How celestials look and move.");

	ImGui::SeparatorText("General");

	ImGui::Checkbox("Custom Celestials", &settings.override_vanilla_celestials);

	if (!settings.override_vanilla_celestials)
		ImGui::BeginDisabled();

	ImGui::SeparatorText("Sun Disc");
	{
		ImGui::ColorEdit3("Color", &settings.sun_disc_color.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		ImGui::SliderAngle("Angular Radius", &settings.sun_angular_radius, 0.05, 5, "%.2f deg", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Ours is quite small (only 0.25 deg)");

		ImGui::Checkbox("Custom Trajectory", &settings.override_sun_traj);
		if (ImGui::TreeNodeEx("Trajectory")) {
			if (!settings.override_sun_traj)
				ImGui::BeginDisabled();

			TrajectoryEdit(settings.sun_trajectory);

			if (!settings.override_sun_traj)
				ImGui::EndDisabled();

			ImGui::TreePop();
		}
	}

	ImGui::SeparatorText("Masser");
	ImGui::PushID("Masser");
	{
		ImGui::SliderFloat("Brightness", &settings.masser_brightness, 0.f, 10.f, "%.2f");
		ImGui::SliderAngle("Angular Radius", &settings.masser_angular_radius, 0.05, 30, "%.2f deg", ImGuiSliderFlags_AlwaysClamp);

		ImGui::Checkbox("Custom Trajectory", &settings.override_masser_traj);
		if (ImGui::TreeNodeEx("Trajectory")) {
			if (!settings.override_masser_traj)
				ImGui::BeginDisabled();

			TrajectoryEdit(settings.masser_trajectory);

			if (!settings.override_masser_traj)
				ImGui::EndDisabled();

			ImGui::TreePop();
		}
	}
	ImGui::PopID();

	ImGui::SeparatorText("Secunda");
	ImGui::PushID("Secunda");
	{
		ImGui::SliderFloat("Brightness", &settings.secunda_brightness, 0.f, 10.f, "%.2f");
		ImGui::SliderAngle("Angular Radius", &settings.secunda_angular_radius, 0.05, 30, "%.2f deg", ImGuiSliderFlags_AlwaysClamp);

		ImGui::Checkbox("Custom Trajectory", &settings.override_secunda_traj);
		if (ImGui::TreeNodeEx("Trajectory")) {
			if (!settings.override_masser_traj)
				ImGui::BeginDisabled();

			TrajectoryEdit(settings.secunda_trajectory);

			if (!settings.override_masser_traj)
				ImGui::EndDisabled();

			ImGui::TreePop();
		}
	}
	ImGui::PopID();

	if (!settings.override_vanilla_celestials)
		ImGui::EndDisabled();
}

void PhysicalSky::SettingsAtmosphere()
{
	ImGui::TextWrapped("The composition and physical properties of the atmosphere.");

	ImGui::SeparatorText("Misc");

	ImGui::SliderFloat("Atmosphere Thickness", &settings.atmos_thickness, 0.f, 200.f, "%.1f km");
	if (auto _tt = Util::HoverTooltipWrapper())
		ImGui::Text("The thickness of atmosphere that contributes to lighting.");

	ImGui::SeparatorText("Air Molecules (Rayleigh)");
	ImGui::PushID("Rayleigh");
	{
		ImGui::TextWrapped(
			"Particles much smaller than the wavelength of light. They have almost complete symmetry in forward and backward scattering (Rayleigh Scattering). "
			"On earth, they are what makes the sky blue and, at sunset, red. Usually needs no extra change.");

		ImGui::SliderFloat3("Scatter", &settings.rayleigh_scatter.x, 0.f, 30.f, "%.3f Mm^-1");
		ImGui::SliderFloat3("Absorption", &settings.rayleigh_absorption.x, 0.f, 30.f, "%.3f Mm^-1");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Usually zero.");
		ImGui::SliderFloat("Height Decay", &settings.rayleigh_decay, 0.f, 2.f);
	}
	ImGui::PopID();

	ImGui::SeparatorText("Aerosol (Mie)");
	ImGui::PushID("Mie");
	{
		ImGui::TextWrapped(
			"Solid and liquid particles greater than 1/10 of the light wavelength but not too much, like dust. Strongly anisotropic (Mie Scattering). "
			"They contributes to the aureole around bright celestial bodies.");

		ImGui::SliderFloat("Anisotropy", &settings.aerosol_phase_func_g, -1, 1);
		ImGui::SliderFloat3("Scatter", &settings.aerosol_scatter.x, 0.f, 30.f, "%.3f Mm^-1");
		ImGui::SliderFloat3("Absorption", &settings.aerosol_absorption.x, 0.f, 30.f, "%.3f Mm^-1");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Usually 1/9 of scatter coefficient. Dust/pollution is lower, fog is higher.");
		ImGui::SliderFloat("Height Decay", &settings.aerosol_decay, 0.f, 2.f);
	}
	ImGui::PopID();

	ImGui::SeparatorText("Ozone");
	ImGui::PushID("Ozone");
	{
		ImGui::TextWrapped(
			"The ozone layer high up in the sky that mainly absorbs light of certain wavelength. "
			"It keeps the zenith sky blue, especially at sunrise or sunset.");

		ImGui::SliderFloat3("Absorption", &settings.ozone_absorption.x, 0.f, 30.f, "%.3f Mm^-1");
		ImGui::DragFloat("Layer Altitude", &settings.ozone_altitude, .1f, 0.f, 100.f, "%.3f km");
		ImGui::DragFloat("Layer Thickness", &settings.ozone_thickness, .1f, 0.f, 50.f, "%.3f km");
	}
	ImGui::PopID();
}

void PhysicalSky::SettingsDebug()
{
	ImGui::TextWrapped("Beep Boop.");

	auto accumulator = RE::BSGraphics::BSShaderAccumulator::GetCurrentAccumulator();
	auto calendar = RE::Calendar::GetSingleton();
	auto sky = RE::Sky::GetSingleton();
	auto sun = sky->sun;
	auto climate = sky->currentClimate;
	auto dir_light = skyrim_cast<RE::NiDirectionalLight*>(accumulator->GetRuntimeData().activeShadowSceneNode->GetRuntimeData().sunLight->light.get());

	RE::NiPoint3 cam_pos = { 0, 0, 0 };
	if (auto cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot)
		cam_pos = cam->cameraRoot->world.translate;

	// ImGui::InputFloat("Timer", &phys_sky_sb_data.timer, 0, 0, "%.6f", ImGuiInputTextFlags_ReadOnly);

	if (calendar) {
		ImGui::SeparatorText("Calendar");

		auto game_time = calendar->GetCurrentGameTime();
		auto game_hour = calendar->GetHour();
		auto game_day = calendar->GetDay();
		auto game_month = calendar->GetMonth();
		auto day_in_year = getDayInYear();
		ImGui::Text("Game Time: %.3f", game_time);
		ImGui::SameLine();
		ImGui::Text("Hour: %.3f", game_hour);
		ImGui::SameLine();
		ImGui::Text("Day: %.3f", game_day);
		ImGui::SameLine();
		ImGui::Text("Month: %u", game_month);
		ImGui::SameLine();
		ImGui::Text("Day in Year: %.3f", day_in_year);

		if (climate) {
			auto sunrise = climate->timing.sunrise;
			auto sunset = climate->timing.sunset;
			ImGui::Text("Sunrise: %u-%u", sunrise.GetBeginTime().tm_hour, sunrise.GetEndTime().tm_hour);
			ImGui::SameLine();
			ImGui::Text("Sunset: %u-%u", sunset.GetBeginTime().tm_hour, sunset.GetEndTime().tm_hour);
			ImGui::SameLine();
		}

		auto vanilla_sun_lerp = getVanillaSunLerpFactor();
		ImGui::SameLine();
		ImGui::Text("Vanilla sun lerp: %.3f", vanilla_sun_lerp);
	}

	ImGui::SeparatorText("Celestials");
	{
		ImGui::InputFloat3("Mod Sun Direction", &phys_sky_sb_data.sun_dir.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
		if (sun) {
			auto sun_dir = sun->sunBase->world.translate - cam_pos;
			sun_dir.Unitize();
			ImGui::InputFloat3("Vanilla Sun Mesh Direction", &sun_dir.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
		}
		if (dir_light) {
			auto dirlight_dir = -dir_light->GetWorldDirection();
			ImGui::InputFloat3("Vanilla Light Direction", &dirlight_dir.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
		}

		ImGui::Text("Masser Phase: %s", magic_enum::enum_name((RE::Moon::Phase)current_moon_phases[0]).data());
		ImGui::InputFloat3("Mod Masser Direction", &phys_sky_sb_data.masser_dir.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
		ImGui::InputFloat3("Mod Masser Up", &phys_sky_sb_data.masser_upvec.x, "%.3f", ImGuiInputTextFlags_ReadOnly);

		ImGui::Text("Secunda Phase: %s", magic_enum::enum_name((RE::Moon::Phase)current_moon_phases[1]).data());
		ImGui::InputFloat3("Mod Secunda Direction", &phys_sky_sb_data.secunda_dir.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
		ImGui::InputFloat3("Mod Secunda Up", &phys_sky_sb_data.secunda_upvec.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
	}

	ImGui::SeparatorText("Textures");
	{
		ImGui::BulletText("Transmittance LUT");
		ImGui::Image((void*)(transmittance_lut->srv.get()), { s_transmittance_width, s_transmittance_height });

		ImGui::BulletText("Multiscatter LUT");
		ImGui::Image((void*)(multiscatter_lut->srv.get()), { s_multiscatter_width, s_multiscatter_height });

		ImGui::BulletText("Sky-View LUT");
		ImGui::Image((void*)(sky_view_lut->srv.get()), { s_sky_view_width, s_sky_view_height });
	}
}