#include "ParametricSky.h"

#include "Globals.h"
#include "State.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	ParametricSky::Settings,
	sunColor,
	ozoneDu,
	turbidity,
	vividness)

////////////////////////////////////////////////////////////////////////////////////

void ParametricSky::RestoreDefaultSettings()
{
	settings = {};
}

void ParametricSky::LoadSettings(json& o_json)
{
	settings = o_json;
}

void ParametricSky::SaveSettings(json& o_json)
{
	o_json = settings;
}

void ParametricSky::DrawSettings()
{
	ImGui::ColorEdit3("Multiplier", &settings.sunColor.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
	ImGui::SliderFloat("Ozone Thickness", &settings.ozoneDu, 250.f, 450.f, "%.1f Dobson Unit");
	ImGui::SliderFloat("Turbidity", &settings.turbidity, 1.f, 50.f, "%.2f");
	ImGui::SliderFloat("Vividness", &settings.vividness, 0.f, 1.f, "%.2f");
}

void ParametricSky::Reset()
{
	constexpr static float kGameUnit2Km = 1.428e-5f;
	constexpr static float kKm2GameUnit = 1 / kGameUnit2Km;

	auto accumulator = RE::BSGraphics::BSShaderAccumulator::GetCurrentAccumulator();

	RE::NiPoint3 posCam = { 0, 0, 0 };
	if (auto cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot)
		posCam = cam->cameraRoot->world.translate;

	RE::NiPoint3 lightDir = { 0, 0, 0 };
	auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(accumulator->GetRuntimeData().activeShadowSceneNode->GetRuntimeData().sunLight->light.get());
	if (dirLight)
		lightDir = -dirLight->GetWorldDirection();

	cbData = {
		.altitude = (posCam.z + 14000) * kGameUnit2Km,
		.sunColor = settings.sunColor,
		.sunAngles = float2(atan2(lightDir.y, lightDir.x), lightDir.z),
		.ozoneDu = settings.ozoneDu,
		.turbidity = settings.turbidity,
		.vividness = settings.vividness,
		.fRayleighZenith = 0,
	};
}