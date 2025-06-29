#pragma once

struct ParametricSky : public Feature
{
	////////////////////////////////////////////////// Boilerplate
	static ParametricSky* GetSingleton()
	{
		static ParametricSky singleton;
		return &singleton;
	}

	// Metadata
	virtual inline std::string GetName() override { return "Parametric Sky"; }
	virtual inline std::string GetShortName() override { return "ParametricSky"; }
	virtual inline std::string_view GetCategory() const override { return "Sky"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL("999999"); }
	virtual inline std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Analytic sky models for photorealistic sky gradients.",
			{
				"Sky.",
				"Cheese.",
			}
		};
	}

	// Functionality
	virtual inline bool SupportsVR() override { return true; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type t) override { return t == RE::BSShader::Type::Sky; };
	virtual inline std::string_view GetShaderDefineName() override { return "PARAMETRIC_SKY"; }

	// Settings & UI
	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void DrawSettings() override;

	// Drawing
	virtual void Reset() override;

	////////////////////////////////////////////////// Feature Specific Data
	struct Settings
	{
		bool enabled = true;
		bool clearSky = false;
		float3 sunColor = float3{ 1.f, 1.f, 1.f };
		float ozoneDu = 300.f;
		float turbidity = 2.f;
		float vividness = 0.f;
		int tonemapper = 2;
		float vanillaMix = 0;
	} settings;
	bool enablePositioner = false;
	float positSunZenith = 0;
	float positSunAzimuth = 0;

	struct PerDirLight
	{
		float3 lightColor;
		float twilightLum;
		float2 lightAngles;  // point towards sun, x: azimuth, y: cos zenith
		float twilightVertScale;
		float twilightDarkness;
		float3 tauSunOzone;
		float anisoMie;
		float3 tauSunRayleigh;
		float g2;
		float3 tauSunMie;
		float cMie;
	};
	static_assert(sizeof(PerDirLight) % 16 == 0);

	struct CbData
	{
		PerDirLight sunData;

		uint enabled;
		uint clearSky;
		float altitude;
		float turbidity;
		float vividness;
		int tonemapper;
		float vanillaMix;
		float _pad0;

		float cosHorDownshift;
		float horDownshift;
		float altDecayRayleigh;
		float altDecayMie;
		float3 rouMie;
		float rouMieAltCorrected;
		float msDegrader;
		float cRayleigh;
		float2 _pad1;
	} cbData;
	static_assert(sizeof(CbData) % 16 == 0);
};