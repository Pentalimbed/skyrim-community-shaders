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

void PhysicalWeather::DrawSettings()
{
	static int pagenum = 0;

	ImGui::Combo("Page", &pagenum, "General\0Quality\0World\0Orbits\0Celestials\0Atmosphere\0Debug\0");

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
	ImGui::DragScalar("Transmittance Steps", ImGuiDataType_U32, &settings.transmittance_step);
	ImGui::DragScalar("Multiscatter Steps", ImGuiDataType_U32, &settings.multiscatter_step);
	ImGui::DragScalar("Multiscatter Sqrt Samples", ImGuiDataType_U32, &settings.multiscatter_sqrt_samples);
	ImGui::DragScalar("Sky View Steps", ImGuiDataType_U32, &settings.skyview_step);
	ImGui::DragFloat("Aerial Perspective Max Dist", &settings.aerial_perspective_max_dist);
	ImGui::TreePop();
}

void PhysicalWeather::DrawSettingsWorld()
{
	ImGui::SliderFloat2("Unit Scale", &settings.unit_scale.x, 0.1f, 50.f);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip(
			"Relative scale of the game length unit compared to real physical ones, used by atmosphere rendering and others.\n"
			"First number controls distances. Renders far landscape less clear and thus look further.\n"
			"Second number affects elevation. Makes vistas on the short mountains in game feel higher.");
	ImGui::InputFloat("Bottom Z", &settings.bottom_z);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip(
			"The lowest elevation of the worldspace you shall reach. In game unit. "
			"You can check it using \"getpos z\" console command.");

	ImGui::SliderFloat("Ground Radius", &settings.ground_radius, 0.f, 10.f);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("The supposed radius of the planet Nirn, or whatever rock you are on. In megameter (10^6 m).");
	ImGui::SliderFloat("Atmosphere Thickness", &settings.atmos_thickness, 0.f, .5f);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("The supposed height of the atmosphere. Beyond this it is all trasparent vaccum. In megameter (10^6 m).");

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
	if (ImGui::TreeNodeEx("Sun", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::ColorEdit3("Sun Color", &settings.sun_color.x, hdr_color_edit_flags);
		ImGui::SliderAngle("Sun Aperture", &settings.sun_aperture_angle, 0.f, 90.f, "%.3f deg");
		ImGui::Combo("Limb Darkening Model", &settings.limb_darken_model, "Disabled\0Neckel (Simple)\0Hestroffer (Accurate)\0");
		ImGui::SliderFloat("Limb Darken Strength", &settings.limb_darken_power, 0.f, 5.f, "%.1f");

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Masser", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderAngle("Masser Aperture", &settings.masser_aperture_angle, 0.f, 90.f, "%.3f deg");
		ImGui::SliderFloat("Masser Brightness", &settings.masser_brightness, 0.f, 5.f, "%.1f");

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Secunda", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderAngle("Secunda Aperture", &settings.secunda_aperture_angle, 0.f, 90.f, "%.3f deg");
		ImGui::SliderFloat("Secunda Brightness", &settings.secunda_brightness, 0.f, 5.f, "%.1f");

		ImGui::TreePop();
	}
}

void PhysicalWeather::DrawSettingsAtmosphere()
{
	ImGui::TextWrapped(
		"Settings that controls the global characteristic of the worldspace atmosphere. "
		"Keep in mind that these are planet-scale parameters, much larger compared to common weather phenomenons.");

	if (ImGui::TreeNodeEx("Mixing", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("In-scatter", &settings.ap_inscatter_mix, 0.f, 1.f);
		ImGui::SliderFloat("View Transmittance", &settings.ap_transmittance_mix, 0.f, 1.f);
		ImGui::SliderFloat("Light Transmittance", &settings.light_transmittance_mix, 0.f, 1.f);

		ImGui::TreePop();
	}

	ImGui::SliderFloat("Atmosphere Thickness", &settings.atmos_thickness, 0.f, .5f);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("The supposed height of the atmosphere. Beyond this it is all trasparent vaccum. In megameter (10^6 m).");

	if (ImGui::TreeNodeEx("Air Molecules (Rayleigh)", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TextWrapped(
			"Particles much smaller than the wavelength of light. They have almost complete symmetry in forward and backward scattering (Rayleigh Scattering). "
			"On earth, they are what makes the sky blue and, at sunset, red. Usually needs no extra change.");

		ImGui::ColorEdit3("Scatter", &settings.rayleigh_scatter.x, hdr_color_edit_flags);
		ImGui::ColorEdit3("Absorption", &settings.rayleigh_absorption.x, hdr_color_edit_flags);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Usually zero.");
		ImGui::DragFloat("Height Decay", &settings.rayleigh_decay, .1f, 0.f, 100.f);

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Aerosol (Mie)", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TextWrapped(
			"Solid and liquid particles greater than 1/10 of the light wavelength but not too much, like dust. Strongly anisotropic (Mie Scattering). "
			"They contributes to the aureole around bright celestial bodies. Increase in dustier weather.");

		ImGui::Combo("Phase Function", &settings.mie_phase_func, "Henyey-Greenstein (Simple)\0Cornette-Shanks (Complex)\0");
		ImGui::SliderFloat("Asymmetry", &settings.mie_asymmetry, -1, 1);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Makes scattered light more concentrated to the back (-1) or the front (1).");
		ImGui::ColorEdit3("Scatter", &settings.mie_scatter.x, hdr_color_edit_flags);
		ImGui::ColorEdit3("Absorption", &settings.mie_absorption.x, hdr_color_edit_flags);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Usually 1/9 of scatter coefficient. Dust/pollution is lower, fog is higher.");
		ImGui::DragFloat("Height Decay", &settings.mie_decay, .1f, 0.f, 100.f);

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Ozone", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TextWrapped(
			"The ozone layer high up in the sky that mainly absorbs light of certain wavelength. "
			"It keeps the zenith sky blue, especially at sunrise or sunset.");

		ImGui::ColorEdit3("Absorption", &settings.ozone_absorption.x, hdr_color_edit_flags);
		ImGui::DragFloat("Layer Height", &settings.ozone_height, .1f, 0.f, 100.f);
		ImGui::DragFloat("Layer Thickness", &settings.ozone_thickness, .1f, 0.f, 50.f);

		ImGui::TreePop();
	}

	ImPlot::SetNextAxesToFit();
	if (ImPlot::BeginPlot("Media Density by Height", { -1, 0 }, ImPlotFlags_NoInputs)) {
		ImPlot::SetupAxis(ImAxis_X1, "Altitude / km");
		ImPlot::SetupAxis(ImAxis_Y1, "Relative Density");
		ImPlot::SetupLegend(ImPlotLocation_NorthEast);

		constexpr size_t n_datapoints = 101;
		std::array<float, n_datapoints> heights, rayleigh_data, mie_data, ozone_data;
		for (size_t i = 0; i < n_datapoints; i++) {
			heights[i] = phys_sky_sb_content.atmos_thickness * 1e3f * i / (n_datapoints - 1);
			rayleigh_data[i] = exp(-heights[i] / settings.rayleigh_decay);
			mie_data[i] = exp(-heights[i] / settings.mie_decay);
			ozone_data[i] = max(0.f, 1 - abs(heights[i] - settings.ozone_height) / (settings.ozone_thickness * .5f));
		}
		ImPlot::PlotLine("Rayleigh", heights.data(), rayleigh_data.data(), n_datapoints);
		ImPlot::PlotLine("Mie", heights.data(), mie_data.data(), n_datapoints);
		ImPlot::PlotLine("Ozone", heights.data(), ozone_data.data(), n_datapoints);
		ImPlot::EndPlot();
	}
}

// void drawTrans(const char* label, RE::NiTransform t)
// {
// 	auto rel_pos = t.translate - RE::PlayerCamera::GetSingleton()->pos;
// 	rel_pos.Unitize();
// 	ImGui::InputFloat3(label, &rel_pos.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
// }

void PhysicalWeather::DrawSettingsDebug()
{
	ImGui::InputFloat("Timer", &phys_sky_sb_content.timer, 0, 0, "%.6f", ImGuiInputTextFlags_ReadOnly);
	ImGui::InputFloat3("Sun Direction", &phys_sky_sb_content.sun_dir.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
	ImGui::InputFloat3("Masser Direction", &phys_sky_sb_content.masser_dir.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
	ImGui::InputFloat3("Masser Up", &phys_sky_sb_content.masser_upvec.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
	ImGui::InputFloat3("Secunda Direction", &phys_sky_sb_content.secunda_dir.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
	ImGui::InputFloat3("Secunda Up", &phys_sky_sb_content.secunda_upvec.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
	ImGui::InputFloat3("Cam Pos", &phys_sky_sb_content.player_cam_pos.x, "%.3f", ImGuiInputTextFlags_ReadOnly);

	ImGui::BulletText("Transmittance LUT");
	ImGui::Image((void*)(transmittance_lut->srv.get()), { s_transmittance_width, s_transmittance_height });

	ImGui::BulletText("Multiscatter LUT");
	ImGui::Image((void*)(multiscatter_lut->srv.get()), { s_multiscatter_width, s_multiscatter_height });

	ImGui::BulletText("Sky-View LUT");
	ImGui::Image((void*)(sky_view_lut->srv.get()), { s_sky_view_width, s_sky_view_height });
}