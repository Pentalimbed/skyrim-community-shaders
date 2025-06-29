#include "ParametricSky.h"

#include "Globals.h"
#include "State.h"
#include "Util.h"

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
	ImGui::Checkbox("Enabled", &settings.enabled);
	if (ImGui::CollapsingHeader("Sky", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::ColorEdit3("Multiplier", &settings.sunColor.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float);
		ImGui::SliderFloat("Ozone Thickness", &settings.ozoneDu, 250.f, 450.f, "%.1f Dobson Unit");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Control the amount of ozone. Makes the twilight zenith blue.");
		ImGui::SliderFloat("Turbidity", &settings.turbidity, 1.f, 64.f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Control the amount of aerosols. Makes the sky murky.");
		ImGui::SliderFloat("Vividness", &settings.vividness, 0.f, 1.f, "%.2f");
	}

	if (ImGui::CollapsingHeader("Post Processing", ImGuiTreeNodeFlags_DefaultOpen)) {
		if (ImGui::BeginTable("tonemap", 4, ImGuiTableFlags_SizingStretchSame, { -FLT_MIN, 0 })) {
			ImGui::TableNextColumn();
			ImGui::Text("Tonemapper");
			ImGui::TableNextColumn();
			ImGui::RadioButton("Linear", &settings.tonemapper, 0);
			ImGui::TableNextColumn();
			ImGui::RadioButton("Gamma", &settings.tonemapper, 1);
			ImGui::TableNextColumn();
			ImGui::RadioButton("Reinherd", &settings.tonemapper, 2);
			ImGui::EndTable();
		}
		ImGui::SliderFloat("Vanilla Mix", &settings.vanillaMix, 0.1f, 1.f, "%.2f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Blend in vanilla sky color.");
	}

	if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Clear Sky", &settings.clearSky);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Remove vanilla clouds, sun and moons.");
		ImGui::Checkbox("Celestial Positioner", &enablePositioner);
		if (!enablePositioner)
			ImGui::BeginDisabled();
		ImGui::SliderAngle("Sun Zenith", &positSunZenith, 0, 180);
		ImGui::SliderAngle("Sun Azimuth", &positSunAzimuth, 0, 360);
		if (!enablePositioner)
			ImGui::EndDisabled();
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////

constexpr float rEarth = 6371;  // in km
// effective ratio of rEarth / layer height
constexpr float rOzoneRel = 125;
constexpr float rRayleighRel = 500;
constexpr float rMieRel = 1500;
// scale height, in km
constexpr float hScaleOzone = 40;
constexpr float hScaleRayleigh = 8.4f;
constexpr float hScaleMie = 1.2f;
// max relative air mass cap at twilight
constexpr float maOzoneCap = 25;
constexpr float maRayleighCap = 75;
constexpr float maMieCap = 200;
// extincition coeffs per rel air mass
constexpr float3 rouOzonePerDu = float3(5e-5f, 5e-5f, 5e-6f);
constexpr float3 rouRayleigh = float3(0.04f, 0.09f, 0.25f);
constexpr float rouMieGreen = 0.06f;

float RelativeAirMass(float theta, float rRel)
{
	float rTemp = rRel * theta;
	return sqrt(rTemp * rTemp + 2 * rRel + 1) - rTemp;
};

void CalculatePerLightData(
	ParametricSky::PerDirLight& lightData,
	float altitude,
	float altDecayOzone, float altDecayRayleigh, float altDecayMie,
	float3 rouOzone, float3 rouMie,
	float msDegrader)
{
	float cosSun = lightData.lightAngles.y;
	float sinSun = sqrt(1 - cosSun * cosSun);

	float hTanSun = altitude + (cosSun > 0 ? 0 : rEarth - rEarth / sinSun);

	float amSunOzone = std::min(RelativeAirMass(cosSun, rOzoneRel), maOzoneCap) * altDecayOzone;
	float amSunRayleigh = std::min(RelativeAirMass(cosSun, rRayleighRel), maRayleighCap) * altDecayRayleigh;
	float amSunMie = std::min(RelativeAirMass(cosSun, rMieRel), maMieCap) * altDecayMie;

	lightData.tauSunOzone = rouOzone * amSunOzone;
	lightData.tauSunRayleigh = rouRayleigh * amSunRayleigh;
	lightData.tauSunMie = rouMie * amSunMie;

	lightData.twilightVertScale = 0.03f * std::max(-hTanSun, 0.f);
	lightData.twilightDarkness = 0.03f * lightData.twilightVertScale * lightData.twilightVertScale;
	float twilightLum = exp(0.3f - 0.05f * amSunRayleigh - 1.7f * lightData.twilightVertScale) + 1e-5f;
	lightData.lightColor *= twilightLum;

	lightData.anisoMie = msDegrader * exp(-0.02f * amSunMie);
	lightData.g2 = lightData.anisoMie * lightData.anisoMie;
	lightData.cMie = 3 * (1 - lightData.g2) / (3 + msDegrader + 2 * msDegrader * lightData.g2);
}

void ParametricSky::Reset()
{
	auto accumulator = RE::BSGraphics::BSShaderAccumulator::GetCurrentAccumulator();

	RE::NiPoint3 posCam = { 0, 0, 0 };
	if (auto cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot)
		posCam = cam->cameraRoot->world.translate;

	RE::NiPoint3 lightDir = { 0, 0, 0 };
	if (enablePositioner) {
		lightDir = { cos(positSunAzimuth) * sin(positSunZenith), sin(positSunAzimuth) * sin(positSunZenith), cos(positSunZenith) };
	} else {
		auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(accumulator->GetRuntimeData().activeShadowSceneNode->GetRuntimeData().sunLight->light.get());
		if (dirLight)
			lightDir = -dirLight->GetWorldDirection();
	}

	cbData = {
		.sunData = {
			.lightColor = settings.sunColor,
			.lightAngles = float2(atan2(lightDir.y, lightDir.x), lightDir.z),
		},
		.enabled = settings.enabled,
		.clearSky = settings.clearSky,
		.altitude = Util::Units::GameUnitsToMeters(posCam.z + 14500) * 1e-3f,
		.turbidity = settings.turbidity,
		.vividness = settings.vividness,
		.tonemapper = settings.tonemapper,
		.vanillaMix = settings.vanillaMix,
	};

	///////////////////////////////////////////////////////////////////////////

	cbData.cosHorDownshift = rEarth / (rEarth + cbData.altitude);
	cbData.horDownshift = acos(cbData.cosHorDownshift);

	float altDecayOzone = exp(-cbData.altitude / hScaleOzone);
	cbData.altDecayRayleigh = exp(-cbData.altitude / hScaleRayleigh);
	cbData.altDecayMie = exp(-cbData.altitude / hScaleMie);

	float3 rouOzone = settings.ozoneDu * rouOzonePerDu;
	cbData.rouMie = float3(rouMieGreen * (cbData.turbidity - 2 + 1 / cbData.turbidity));
	cbData.rouMieAltCorrected = cbData.rouMie.y * cbData.altDecayMie;
	cbData.msDegrader = 0.94f * exp(-cbData.rouMieAltCorrected);
	float rouMieSkew = 0.9f - 0.1f * cbData.msDegrader;
	cbData.rouMie *= float3(rouMieSkew, 1, 1.f / rouMieSkew);
	cbData.cRayleigh = 3 / (3 + cbData.msDegrader);

	// Per light
	CalculatePerLightData(cbData.sunData,
		cbData.altitude,
		altDecayOzone, cbData.altDecayRayleigh, cbData.altDecayMie,
		rouOzone, cbData.rouMie,
		cbData.msDegrader);
}