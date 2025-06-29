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
	ImGui::ColorEdit3("Multiplier", &settings.sunColor.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_DisplayHSV | ImGuiColorEditFlags_Float);
	ImGui::SliderFloat("Ozone Thickness", &settings.ozoneDu, 250.f, 450.f, "%.1f Dobson Unit");
	ImGui::SliderFloat("Turbidity", &settings.turbidity, 1.f, 64.f, "%.2f");
	ImGui::SliderFloat("Vividness", &settings.vividness, 0.f, 1.f, "%.2f");

	ImGui::SeparatorText("Debug");

	ImGui::Checkbox("Celestial Positioner", &enablePositioner);
	if (!enablePositioner)
		ImGui::BeginDisabled();
	ImGui::SliderAngle("Sun Zenith", &positSunZenith, 0, 180);
	ImGui::SliderAngle("Sun Azimuth", &positSunAzimuth, 0, 360);
	if (!enablePositioner)
		ImGui::EndDisabled();
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
		.altitude = Util::Units::GameUnitsToMeters(posCam.z + 14500) * 1e-3f,
		.sunColor = settings.sunColor,
		.sunAngles = float2(atan2(lightDir.y, lightDir.x), lightDir.z),
		.turbidity = settings.turbidity,
		.vividness = settings.vividness,
		.fRayleighZenith = 0,
	};

	///////////////////////////////////////////////////////////////////////////

	auto RelativeAirMass = [](float theta, float rRel) {
		float rTemp = rRel * theta;
		return sqrt(rTemp * rTemp + 2 * rRel + 1) - rTemp;
	};

	auto radians = [](float deg) { return deg * (3.1415926535f / 180); };

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

	cbData.cosHorDownshift = rEarth / (rEarth + cbData.altitude);
	float cosSun = cbData.sunAngles.y;
	float sinSun = sqrt(1 - cosSun * cosSun);

	float hTanSun = cbData.altitude + (cosSun > 0 ? 0 : rEarth - rEarth / sinSun);

	float altDecayOzone = exp(-cbData.altitude / hScaleOzone);
	cbData.altDecayRayleigh = exp(-cbData.altitude / hScaleRayleigh);
	cbData.altDecayMie = exp(-cbData.altitude / hScaleMie);

	float amSunOzone = std::min(RelativeAirMass(cosSun, rOzoneRel), maOzoneCap) * altDecayOzone;
	float amSunRayleigh = std::min(RelativeAirMass(cosSun, rRayleighRel), maRayleighCap) * cbData.altDecayRayleigh;
	float amSunMie = std::min(RelativeAirMass(cosSun, rMieRel), maMieCap) * cbData.altDecayMie;

	float3 rouOzone = settings.ozoneDu * rouOzonePerDu;
	cbData.rouMie = float3(rouMieGreen * (cbData.turbidity - 2 + 1 / cbData.turbidity));
	cbData.rouMieAltCorrected = cbData.rouMie.y * cbData.altDecayMie;
	cbData.msDegrader = 0.94f * exp(-cbData.rouMieAltCorrected);
	float rouMieSkew = 0.9f - 0.1f * cbData.msDegrader;
	cbData.rouMie *= float3(rouMieSkew, 1, 1.f / rouMieSkew);

	cbData.tauSunOzone = rouOzone * amSunOzone;
	cbData.tauSunRayleigh = rouRayleigh * amSunRayleigh;
	cbData.tauSunMie = cbData.rouMie * amSunMie;

	cbData.twilightVertScale = 0.03f * std::max(-hTanSun, 0.f);
	cbData.twilightDarkness = 0.03f * cbData.twilightVertScale * cbData.twilightVertScale;
	// float twilightLum = exp(0.3 - 0.05 * amSunRayleigh - 1.7 * twilightVertScale) + 1e-5; // for absolute luminance
	cbData.deepTwilightCutoff = 0.002f * cbData.altDecayRayleigh;

	float zenithSun = acos(cosSun);
	cbData.horDownshift = acos(cbData.cosHorDownshift);
	cbData.venusBeltShadowThres = radians(89) - cbData.horDownshift - zenithSun;
	cbData.venusBeltAltCorrection = (radians(2) + cbData.horDownshift) / radians(120);

	cbData.anisoMie = cbData.msDegrader * exp(-0.02f * amSunMie);
	cbData.cRayleigh = 3 / (3 + cbData.msDegrader);
	cbData.g2 = cbData.anisoMie * cbData.anisoMie;
	cbData.cMie = 3 * (1 - cbData.g2) / (3 + cbData.msDegrader + 2 * cbData.msDegrader * cbData.g2);
}