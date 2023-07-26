#include "../PhysicalWeather.h"
#include "PhysicalWeather_Common.h"

#include <implot.h>

constexpr auto hdr_color_edit_flags = ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR;

void OrbitEdit(Orbit& orbit)
{
	ImGui::SliderAngle("Azimuth", &orbit.azimuth, -180, 180);
	ImGui::SliderAngle("Zenith", &orbit.zenith, -90, 90);
	ImGui::SliderFloat("Offset", &orbit.offset, -1, 1);
}

void TrajectoryEdit(Trajectory& traj)
{
	if (ImGui::TreeNodeEx("Low Orbit")) {
		OrbitEdit(traj.minima);
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("High Orbit")) {
		OrbitEdit(traj.maxima);
		ImGui::TreePop();
	}
	ImGui::InputFloat("Dirunal Period", &traj.period_dirunal, 0, 0, "%.3f day(s)");
	traj.period_dirunal = max(1e-6f, traj.period_dirunal);
	ImGui::SliderFloat("Dirunal Offset", &traj.offset_dirunal, -traj.period_dirunal, traj.period_dirunal, "%.3f day(s)");
	ImGui::InputFloat("Orbit Shift Period", &traj.period_shift), 0, 0, "%.1f day(s)";
	traj.period_shift = max(1e-6f, traj.period_shift);
	ImGui::SliderFloat("Orbit Shift Offset", &traj.offset_shift, -traj.period_shift, traj.period_shift, "%.1f day(s)");
}

void PhaseFuncEdit(PhaseFunc& phase)
{
	ImGui::Combo("Phase Function", &phase.func, "Henyey-Greenstein\0Henyey-Greenstein (Dual-Lobe)\0Cornette-Shanks\0Jendersie-d'Eon (Complex)\0");
	ImGui::Indent();
	switch (phase.func) {
	case 0:
	case 2:
		ImGui::SliderFloat("Asymmetry", &phase.g0, -1, 1);
		break;
	case 1:
		ImGui::SliderFloat("Forward Asymmetry", &phase.g0, 0, 1);
		ImGui::SliderFloat("Backward Asymmetry 1", &phase.g1, -1, 0);
		ImGui::SliderFloat("Backward Weight", &phase.w, 0, 1);
		break;
	case 3:
		ImGui::SliderFloat("Particle Diameter", &phase.d, 2, 20, "%.1f um");
		break;
	default:
		break;
	}
	ImGui::Unindent();
}

void CloudLayerEdit(CloudLayer& clouds)
{
	ImGui::SliderFloat2("Height Range", &clouds.height_range.x, 0, 20, "%.2f");
	ImGui::SliderFloat3("Frequency", &clouds.freq.x, 0, 10.0);
	ImGui::ColorEdit3("Scatter", &clouds.scatter.x, hdr_color_edit_flags);
	ImGui::ColorEdit3("Absorption", &clouds.absorption.x, hdr_color_edit_flags);
}

void PhysicalWeather::DrawSettings()
{
	static int pagenum = 0;

	ImGui::Combo("Page", &pagenum, "General\0Quality\0World\0Orbits\0Celestials\0Atmosphere\0Clouds\0Debug\0");

	ImGui::Separator();

	switch (pagenum) {
	case 0:
		DrawSettingsGeneral();
		break;
	case 1:
		DrawSettingsQuality();
		break;
	case 2:
		DrawSettingsWorld();
		break;
	case 3:
		DrawSettingsOrbits();
		break;
	case 4:
		DrawSettingsCelestials();
		break;
	case 5:
		DrawSettingsAtmosphere();
		break;
	case 6:
		DrawSettingsClouds();
		break;
	case 7:
		DrawSettingsDebug();
		break;
	default:
		break;
	}
}

void PhysicalWeather::DrawSettingsGeneral()
{
	ImGui::TextWrapped(
		"Some settings are shared across many pages, for the sake of convenience. "
		"If they have the same name, they are the same setting. ");

	ImGui::Checkbox("Enable Physcial Sky", &settings.enable_sky);

	ImGui::Checkbox("Enable Aerial Perspective", &settings.enable_scatter);
	ImGui::Checkbox("Enable Tonemapping", &settings.enable_tonemap);
	if (settings.enable_tonemap)
		ImGui::SliderFloat("Tonemap Exposure", &settings.tonemap_keyval, 0.f, 2.f);
}

void PhysicalWeather::DrawSettingsQuality()
{
	ImGui::TextWrapped(
		"QUALITY\n"
		"Tradeoff options between better visual and better performance.");
	ImGui::Separator();

	ImGui::DragScalar("Transmittance Steps", ImGuiDataType_U32, &settings.transmittance_step);
	ImGui::DragScalar("Multiscatter Steps", ImGuiDataType_U32, &settings.multiscatter_step);
	ImGui::DragScalar("Multiscatter Sqrt Samples", ImGuiDataType_U32, &settings.multiscatter_sqrt_samples);
	ImGui::DragScalar("Sky View Steps", ImGuiDataType_U32, &settings.skyview_step);
	ImGui::SliderFloat("Aerial Perspective Max Dist", &settings.aerial_perspective_max_dist, 0, settings.atmos_thickness, "%.3f km");

	ImGui::DragScalar("Cloud March Steps", ImGuiDataType_U32, &settings.cloud_march_step);

	ImGui::TreePop();
}

void PhysicalWeather::DrawSettingsWorld()
{
	ImGui::TextWrapped(
		"WORLD\n"
		"The planetary properties of Nirn, or a macroscopic part of Nirn that is nearest to you.");
	ImGui::Separator();

	ImGui::SliderFloat("Unit Scale", &settings.unit_scale, 0.1f, 50.f);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip(
			"Relative scale of the game length unit compared to real physical ones, used by atmosphere rendering and others.\n"
			"First number controls distances. Renders far landscape less clear and thus look further.\n"
			"Second number affects elevation. Makes vistas on the short mountains in game feel higher.");
	ImGui::InputFloat("Bottom Z", &settings.bottom_z, 0, 0, "%.3f game unit");
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip(
			"The lowest elevation of the worldspace you shall reach. "
			"You can check it using \"getpos z\" console command.");

	ImGui::SliderFloat("Ground Radius", &settings.ground_radius, 0.f, 10.f, "%.3f km");
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("The supposed radius of the planet Nirn, or whatever rock you are on. ");
	ImGui::SliderFloat("Atmosphere Thickness", &settings.atmos_thickness, 0.f, .5f, "%.3f km");
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("The supposed height of the atmosphere. Beyond this it is all trasparent vaccum. ");

	ImGui::ColorEdit3("Ground Albedo", &settings.ground_albedo.x, hdr_color_edit_flags);

	ImGui::ColorEdit3("Sunlight Color", &settings.sunlight_color.x, hdr_color_edit_flags);
	ImGui::ColorEdit3("Moonlight Color", &settings.moonlight_color.x, hdr_color_edit_flags);
}

void PhysicalWeather::DrawSettingsOrbits()
{
	ImGui::TextWrapped(
		"ORBITS\n"
		"Controling how celestials move across the sky at different time of the day/year.");
	ImGui::Separator();

	ImGui::SliderAngle("Critical Sun Angle", &settings.critcial_sun_angle, 0, 90);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("When the sun dips this much below the horizon, the sky will be lit by moonlight instead.");

	if (ImGui::TreeNodeEx("Sun", ImGuiTreeNodeFlags_DefaultOpen)) {
		TrajectoryEdit(settings.sun_trajectory);
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("Masser", ImGuiTreeNodeFlags_DefaultOpen)) {
		TrajectoryEdit(settings.masser_trajectory);
		ImGui::TreePop();
	}
	if (ImGui::TreeNodeEx("Secunda", ImGuiTreeNodeFlags_DefaultOpen)) {
		TrajectoryEdit(settings.secunda_trajectory);
		ImGui::TreePop();
	}
}

void PhysicalWeather::DrawSettingsCelestials()
{
	ImGui::TextWrapped(
		"CELESTIALS\n"
		"Controling how celestials look by it self, its shape and brightness, etc.");
	ImGui::Separator();

	if (ImGui::TreeNodeEx("Sun", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::ColorEdit3("Color", &settings.sun_color.x, hdr_color_edit_flags);
		ImGui::SliderAngle("Aperture", &settings.sun_aperture_angle, 0.f, 45.f, "%.3f deg");
		ImGui::Combo("Limb Darkening Model", &settings.limb_darken_model, "Disabled\0Neckel (Simple)\0Hestroffer (Accurate)\0");
		ImGui::SliderFloat("Limb Darken Strength", &settings.limb_darken_power, 0.f, 5.f, "%.1f");

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Masser", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderAngle("Aperture", &settings.masser_aperture_angle, 0.f, 45.f, "%.3f deg");
		ImGui::SliderFloat("Brightness", &settings.masser_brightness, 0.f, 5.f, "%.1f");

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Secunda", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderAngle("Aperture", &settings.secunda_aperture_angle, 0.f, 45.f, "%.3f deg");
		ImGui::SliderFloat("Brightness", &settings.secunda_brightness, 0.f, 5.f, "%.1f");

		ImGui::TreePop();
	}

	ImGui::SliderFloat("Stars Brightness", &settings.stars_brightness, 0.f, 1.f, "%.2f");
}

void PhysicalWeather::DrawSettingsAtmosphere()
{
	ImGui::TextWrapped(
		"ATMOSPHERE\n"
		"What the air above and around you is composed of, and how thick it is.");
	ImGui::Separator();

	if (ImGui::TreeNodeEx("Mixing", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("In-scatter", &settings.ap_inscatter_mix, 0.f, 1.f);
		ImGui::SliderFloat("View Transmittance", &settings.ap_transmittance_mix, 0.f, 1.f);
		ImGui::SliderFloat("Light Transmittance", &settings.light_transmittance_mix, 0.f, 1.f);

		ImGui::TreePop();
	}

	ImGui::SliderFloat("Atmosphere Thickness", &settings.atmos_thickness, 0.f, .5f, "%.3f km");
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("The supposed height of the atmosphere. Beyond this it is all trasparent vaccum.");

	if (ImGui::TreeNodeEx("Air Molecules (Rayleigh)", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TextWrapped(
			"Particles much smaller than the wavelength of light. They have almost complete symmetry in forward and backward scattering (Rayleigh Scattering). "
			"On earth, they are what makes the sky blue and, at sunset, red. Usually needs no extra change.");

		ImGui::ColorEdit3("Scatter", &settings.rayleigh_scatter.x, hdr_color_edit_flags);
		ImGui::ColorEdit3("Absorption", &settings.rayleigh_absorption.x, hdr_color_edit_flags);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Usually zero.");
		ImGui::SliderFloat("Height Decay", &settings.rayleigh_decay, 0.f, 2.f);

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Aerosol (Mie)", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TextWrapped(
			"Solid and liquid particles greater than 1/10 of the light wavelength but not too much, like dust. Strongly anisotropic (Mie Scattering). "
			"They contributes to the aureole around bright celestial bodies. Increase in dustier weather.");

		PhaseFuncEdit(settings.aerosol_phase_func);
		ImGui::ColorEdit3("Scatter", &settings.aerosol_scatter.x, hdr_color_edit_flags);
		ImGui::ColorEdit3("Absorption", &settings.aerosol_absorption.x, hdr_color_edit_flags);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Usually 1/9 of scatter coefficient. Dust/pollution is lower, fog is higher.");
		ImGui::SliderFloat("Height Decay", &settings.aerosol_decay, 0.f, 2.f);

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Ozone", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TextWrapped(
			"The ozone layer high up in the sky that mainly absorbs light of certain wavelength. "
			"It keeps the zenith sky blue, especially at sunrise or sunset.");

		ImGui::ColorEdit3("Absorption", &settings.ozone_absorption.x, hdr_color_edit_flags);
		ImGui::DragFloat("Layer Height", &settings.ozone_height, .1f, 0.f, 100.f, "%.3f km");
		ImGui::DragFloat("Layer Thickness", &settings.ozone_thickness, .1f, 0.f, 50.f, "%.3f km");

		ImGui::TreePop();
	}

	ImPlot::SetNextAxesToFit();
	if (ImPlot::BeginPlot("Media Density by Height", { -1, 0 }, ImPlotFlags_NoInputs)) {
		ImPlot::SetupAxis(ImAxis_X1, "Altitude / km");
		ImPlot::SetupAxis(ImAxis_Y1, "Relative Density");
		ImPlot::SetupLegend(ImPlotLocation_NorthEast);

		constexpr size_t n_datapoints = 101;
		std::array<float, n_datapoints> heights, rayleigh_data, aerosol_data, ozone_data;
		for (size_t i = 0; i < n_datapoints; i++) {
			heights[i] = phys_weather_sb_content.atmos_thickness * i / (n_datapoints - 1);
			rayleigh_data[i] = exp(-heights[i] / settings.rayleigh_decay);
			aerosol_data[i] = exp(-heights[i] / settings.aerosol_decay);
			ozone_data[i] = max(0.f, 1 - abs(heights[i] - settings.ozone_height) / (settings.ozone_thickness * .5f));
		}
		ImPlot::PlotLine("Rayleigh", heights.data(), rayleigh_data.data(), n_datapoints);
		ImPlot::PlotLine("Mie", heights.data(), aerosol_data.data(), n_datapoints);
		ImPlot::PlotLine("Ozone", heights.data(), ozone_data.data(), n_datapoints);
		ImPlot::EndPlot();
	}
}

void PhysicalWeather::DrawSettingsClouds()
{
	PhaseFuncEdit(settings.cloud_phase_func);
	CloudLayerEdit(settings.cloud_layer);
}

void PhysicalWeather::DrawSettingsDebug()
{
	ImGui::TextWrapped(
		"DEBUG\n"
		"Mines crypto out of your machine...joking.");
	ImGui::Separator();

	ImGui::InputFloat("Timer", &phys_weather_sb_content.timer, 0, 0, "%.6f", ImGuiInputTextFlags_ReadOnly);
	ImGui::InputFloat3("Sun Direction", &phys_weather_sb_content.sun_dir.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
	ImGui::InputFloat3("Masser Direction", &phys_weather_sb_content.masser_dir.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
	ImGui::InputFloat3("Masser Up", &phys_weather_sb_content.masser_upvec.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
	ImGui::InputFloat3("Secunda Direction", &phys_weather_sb_content.secunda_dir.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
	ImGui::InputFloat3("Secunda Up", &phys_weather_sb_content.secunda_upvec.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
	ImGui::InputFloat3("Cam Pos", &phys_weather_sb_content.player_cam_pos.x, "%.3f", ImGuiInputTextFlags_ReadOnly);

	ImGui::BulletText("Transmittance LUT");
	ImGui::Image((void*)(transmittance_lut->srv.get()), { s_transmittance_width, s_transmittance_height });

	ImGui::BulletText("Multiscatter LUT");
	ImGui::Image((void*)(multiscatter_lut->srv.get()), { s_multiscatter_width, s_multiscatter_height });

	ImGui::BulletText("Sky-View LUT");
	ImGui::Image((void*)(sky_view_lut->srv.get()), { s_sky_view_width, s_sky_view_height });

	ImGui::BulletText("Cloud scatter");
	ImGui::Image((void*)(cloud_scatter_tex->srv.get()), { 1920 * s_volume_resolution_scale, 1080 * s_volume_resolution_scale });

	ImGui::BulletText("Cloud Transmittance");
	ImGui::Image((void*)(cloud_transmittance_tex->srv.get()), { 1920 * s_volume_resolution_scale, 1080 * s_volume_resolution_scale });
}