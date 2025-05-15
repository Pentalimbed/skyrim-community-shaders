#include "Features/PhysicalWeather.h"

#include "Menu.h"

void PhysicalWeather::DrawSettings()
{
	if (ImGui::BeginTabBar("##PHYSSKY")) {
		if (ImGui::BeginTabItem("Debug")) {
			SettingsDebug();
			ImGui::EndTabItem();
		}

		ImGui::EndTabBar();
	}
}

void PhysicalWeather::SettingsDebug()
{
	ImGui::TextWrapped("Beep Boop.");

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