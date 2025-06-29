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
		float3 sunColor = float3{ 1.f, 1.f, 1.f };
		float ozoneDu = 300.f;
		float turbidity = 2.f;
		float vividness = 0.f;
	} settings;
	bool enablePositioner = false;
	float positSunZenith = 0;
	float positSunAzimuth = 0;

	struct CbData
	{
		float altitude;
		float3 sunColor;
		float2 sunAngles;  // point towards sun, x: azimuth, y: cos zenith
		float turbidity;
		float vividness;  //
		float fRayleighZenith;

		float cosHorDownshift;
		float altDecayRayleigh;
		float altDecayMie;

		float3 rouMie;
		float rouMieAltCorrected;

		float3 tauSunOzone;
		float msDegrader;
		float3 tauSunRayleigh;
		float cRayleigh;
		float3 tauSunMie;
		float anisoMie;

		float horDownshift;
		float g2;
		float cMie;
		float deepTwilightCutoff;

		float twilightVertScale;
		float twilightDarkness;
		float venusBeltShadowThres;
		float venusBeltAltCorrection;
	} cbData;
	static_assert(sizeof(CbData) % 16 == 0);
};