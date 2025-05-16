#include "Features/PhysicalWeather.h"

#include "Menu.h"

void PhysicalWeather::DrawSettings()
{
	if (ImGui::BeginTabBar("##PHYSSKY")) {
		if (ImGui::BeginTabItem("General")) {
			SettingsGeneral();
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

void PhysicalWeather::SettingsGeneral()
{
	if (ImGui::BeginTable("Info", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame, { -1, 0 })) {
		ImGui::TableNextColumn();
		ImGui::Text("Shader Status: ");
		ImGui::TableNextColumn();
		if (ShadersOK())
			ImGui::TextColored({ 0, 1, 0, 1 }, "OK");
		else
			ImGui::TextColored({ 1, 0, 0, 1 }, "ERROR");

		ImGui::TableNextColumn();
		ImGui::Text("Worldspace: ");
		ImGui::TableNextColumn();
		if (globals::game::tes)
			if (auto worldspace = globals::game::tes->GetRuntimeData2().worldSpace; worldspace) {
				std::string worldspaceName = worldspace->GetFormEditorID();
				if (settings.worldspaceWhitelist.contains(worldspaceName))
					ImGui::Text("%s (Enabled)", worldspaceName.c_str());
				else
					ImGui::Text("%s (Disabled)", worldspaceName.c_str());
			}

		ImGui::EndTable();
	}

	ImGui::Checkbox("Enabled", &settings.enabled);
}

void PhysicalWeather::SettingsAtmosphere()
{
	if (ImGui::BeginTable("Info", 1, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingStretchSame, { -1, 0 })) {
		ImGui::TableNextColumn();
		ImGui::TextWrapped("The composition and physical properties of the atmosphere.");
		ImGui::EndTable();
	}

	if (ImGui::CollapsingHeader("Air Molecules (Rayleigh)")) {
		ImGui::PushID("Rayleigh");
		ImGui::TextWrapped(
			"Particles much smaller than the wavelength of light. They have almost complete symmetry in forward and backward scattering. "
			"On earth, they are what makes the sky blue and, at sunset, red. Usually needs no extra change.");

		ImGui::ColorEdit3("Scatter", &settings.rayleighScatter.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		ImGui::SliderFloat("Falloff", &settings.rayleighFalloff, 0.f, 2.f, "%.2f km^-1");
		ImGui::PopID();
	}

	if (ImGui::CollapsingHeader("Aerosol (Mie)")) {
		ImGui::PushID("Mie");
		ImGui::TextWrapped(
			"Solid and liquid particles greater than 1/10 of the light wavelength but not too much, like dust. Strongly anisotropic (Mie Scattering). "
			"They contributes to the aureole around bright celestial bodies.");

		ImGui::SliderFloat("Anisotropy", &settings.aerosolPhaseG, -1, 1);
		ImGui::ColorEdit3("Scatter", &settings.aerosolScatter.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		ImGui::ColorEdit3("Absorption", &settings.aerosolAbsorption.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Usually 1/9 of scatter coefficient. Dust/pollution is lower, fog is higher.");
		ImGui::SliderFloat("Falloff", &settings.aerosolFalloff, 0.f, 2.f, "%.2f km^-1");
		ImGui::PopID();
	}

	if (ImGui::CollapsingHeader("Ozone")) {
		ImGui::PushID("Ozone");
		ImGui::TextWrapped(
			"The ozone layer high up in the sky that mainly absorbs light of certain wavelength. "
			"It keeps the zenith sky blue, especially at sunrise or sunset.");

		ImGui::ColorEdit3("Absorption", &settings.ozoneAbsorption.x, ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		ImGui::DragFloat("Mean Altitude", &settings.ozoneAltitude, .1f, 0.f, 100.f, "%.3f km");
		ImGui::DragFloat("Layer Thickness", &settings.ozoneThickness, .1f, 0.f, 50.f, "%.3f km");
		ImGui::PopID();
	}
}

void PhysicalWeather::SettingsDebug()
{
	if (ImGui::BeginTable("Info", 1, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_SizingStretchSame, { -1, 0 })) {
		ImGui::TableNextColumn();
		ImGui::TextWrapped("Beep Boop.");
		ImGui::EndTable();
	}

	if (ImGui::Button("Recompile Shaders"))
		ClearShaderCache();

	ImGui::SeparatorText("Textures");
	{
		static float debugScale = 0.2f;
		ImGui::SliderFloat("View Scale", &debugScale, 0.1f, 1.f);

		BUFFER_VIEWER_NODE_BULLET(texTrLut, 1.f);
		BUFFER_VIEWER_NODE_BULLET(texMsLut, 1.f);
		BUFFER_VIEWER_NODE_BULLET(texSvLut, 1.f);

		BUFFER_VIEWER_NODE_BULLET(texMainViewTr, debugScale);
		BUFFER_VIEWER_NODE_BULLET(texMainViewLum, debugScale);
	}
}