#include "AdvancedWeather.h"

#include "Globals.h"
#include "State.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	AdvancedWeather::Settings,
	cloudDensity,
	cloudColor,
	precipitationIntensity)

////////////////////////////////////////////////////////////////////////////////////

void AdvancedWeather::RestoreDefaultSettings()
{
	settings = {};
}

void AdvancedWeather::LoadSettings(json& o_json)
{
	settings = o_json;
}

void AdvancedWeather::SaveSettings(json& o_json)
{
	o_json = settings;
}

void AdvancedWeather::DrawSettings()
{
	ImGui::SeparatorText("Cloud Settings");
	ImGui::SliderFloat("Cloud Density", &settings.cloudDensity, 0.0f, 1.0f);
	ImGui::ColorEdit3("Cloud Color", &settings.cloudColor.x);

	ImGui::SeparatorText("Precipitation Settings");
	ImGui::SliderFloat("Precipitation Intensity", &settings.precipitationIntensity, 0.0f, 1.0f);
}

void AdvancedWeather::SetupResources()
{
	// auto renderer = globals::game::renderer;
	// auto device = globals::d3d::device;

	logger::debug("AdvancedWeather: Creating buffers...");
	{
		weatherCb = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<CbData>());
	}

	logger::debug("AdvancedWeather: Resources setup complete");
}

void AdvancedWeather::ClearShaderCache()
{
	// Cleanup if needed
}

void AdvancedWeather::CompileShaders()
{
	// Shader compilation logic would go here
}
