#pragma once

#include "Buffer.h"
#include "Feature.h"
#include "PhysicalWeather/WeatherSim.h"

#include <map>

// TODO: Compatibility with SkySync & IBL
struct PhysicalWeather : Feature
{
	static PhysicalWeather* GetSingleton()
	{
		static PhysicalWeather singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Physical Weather"; }
	virtual inline std::string GetShortName() override { return "PhysicalWeather"; }
	virtual inline std::string_view GetShaderDefineName() override { return "PHYS_WEATHER"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type type) override
	{
		switch (type) {
		case RE::BSShader::Type::Sky:  // disable vanilla sky and clouds when enabled
			return true;
		default:
			return false;
		}
	}

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void RestoreDefaultSettings() override;

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileShaders();
	bool ShadersOK();

	virtual void Reset() override;  // cpu update

	virtual void Prepass() override;  // gpu render
	void GenerateLuts();
	void TestBallTexGen();
	void RenderCloudShadow();
	void RenderMainView();

	virtual void DrawSettings() override;
	void SettingsGeneral();
	void SettingsAtmosphere();
	void SettingsClouds();
	void SettingsDebug();

	////////////////////////////////////////////////////////////////////////

	constexpr static float kGameUnit2Km = 1.428e-5f;
	constexpr static float kKm2GameUnit = 1 / kGameUnit2Km;

	constexpr static uint16_t kTrLutW = 256;
	constexpr static uint16_t kTrLutH = 64;
	constexpr static uint16_t kMsLutW = 32;
	constexpr static uint16_t kMsLutH = 32;
	constexpr static uint16_t kSvLutW = 200;
	constexpr static uint16_t kSvLutH = 150;
	constexpr static uint16_t kApLutW = 32;
	constexpr static uint16_t kApLutH = 32;
	constexpr static uint16_t kApLutD = 32;

	constexpr static uint16_t kCloudW = 256;
	constexpr static uint16_t kCloudH = 256;
	constexpr static uint16_t kCloudD = 256;
	constexpr static float3 kCloudRange = { 10.f, 10.f, 10.f };  // in km

	struct WorldspaceInfo
	{
		float zBottom = -14500.f;
		float2 centre = { 0.f, 0.f };
	};

	struct Settings
	{
		bool enabled = true;

		float3 sunlightColor = float3{ 1.0f, 0.97f, 0.95f } * 6.f;

		std::map<std::string, WorldspaceInfo> worldspaceWhitelist = {
			{ "Tamriel", { -14500.f } }
		};
		float3 groundAlbedo = { .2f, .2f, .2f };

		float rayleighFalloff = 1 / 8.69645f;                    // in km^-1
		float3 rayleighScatter = { 6.6049f, 12.345f, 29.413f };  // in megameter^-1
		float aerosolFalloff = 1 / 1.2f;
		float aerosolPhaseG = 0.8f;
		float3 aerosolScatter = { 3.996f, 3.996f, 3.996f };
		float3 aerosolAbsorption = { .444f, .444f, .444f };
		float ozoneAltitude = 22.3499f + 35.66071f * .5f;  // in km
		float ozoneThickness = 35.66071f;
		float3 ozoneAbsorption = { 2.2911f, 1.5404f, 0 };

		float cloudDensityScale = 1.f;
		float cloudNoiseScale = 0.5f;                // in km
		float3 cloudScatter = { 60.f, 60.f, 60.f };  // in km^-1
		float3 cloudAbsorption = { 0.f, 0.f, 0.f };
	} settings;

	struct CbData
	{
		// DYNAMIC
		float2 texDim;
		float2 rcpTexDim;
		float2 frameDim;
		float2 rcpFrameDim;

		float zCameraPlanet;
		float3 lightDir;  //
		float3 lightColor;

		// WORLD
		uint enabled;  //
		float zBottom;
		float2 centre;
		float rPlanet;  //
		float rAtmosphere;
		float3 groundAlbedo;  //

		// ATMOSPHERE
		float rayleighFalloff;
		float3 rayleighScatter;  //

		float aerosolFalloff;
		float aerosolPhaseG;
		float2 _pad0;  //
		float3 aerosolScatter;
		float _pad1;  //
		float3 aerosolAbsorption;

		float ozoneAltitude;  //
		float ozoneThickness;
		float3 ozoneAbsorption;  //

		// CLOUDS
		float cloudDensityScale;
		float3 cloudScatter;  //
		float cloudNoiseFreq;
		float3 cloudAbsorption;  //
	} cbData;
	static_assert(sizeof(CbData) % 16 == 0);

	eastl::unique_ptr<Texture2D> texTrLut = nullptr;  // transmittance
	eastl::unique_ptr<Texture2D> texMsLut = nullptr;  // multiscattering
	eastl::unique_ptr<Texture2D> texSvLut = nullptr;  // sky view
	eastl::unique_ptr<Texture3D> texApLut = nullptr;  // aerial perspective
	eastl::unique_ptr<Texture3D> texCloudProfile = nullptr;
	eastl::unique_ptr<Texture3D> texCloudSdf = nullptr;
	eastl::unique_ptr<Texture3D> texCloudShadow = nullptr;
	eastl::unique_ptr<Texture2D> texMainViewTr = nullptr;
	eastl::unique_ptr<Texture2D> texMainViewLum = nullptr;
	winrt::com_ptr<ID3D11ShaderResourceView> srvCloudNoise = nullptr;

	winrt::com_ptr<ID3D11SamplerState> sampTr = nullptr;
	winrt::com_ptr<ID3D11SamplerState> sampSv = nullptr;
	winrt::com_ptr<ID3D11SamplerState> sampNoise = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> csTrLutGen = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csMsLutGen = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csSvLutGen = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csApLutGen = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csCloudShadow = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csMainView = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> csTestBall = nullptr;

	WeatherSim weatherSim = {};
};