#pragma once

#include "Buffer.h"
#include "Feature.h"

struct SnowSparkles : Feature
{
	static SnowSparkles* GetSingleton()
	{
		static SnowSparkles singleton;
		return &singleton;
	}

	virtual inline std::string GetName() override { return "Snow Sparkles"; }
	virtual inline std::string GetShortName() override { return "SnowSparkles"; }
	virtual inline std::string_view GetShaderDefineName() override { return "SNOW_SPARKLES"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type t) override { return t == RE::BSShader::Type::Lighting; }

	constexpr static uint32_t c_noise_tex_size = 512;

	struct Settings
	{
		float screen_space_scale = 1.5;
		float log_microfacet_density = 18;
		float microfacet_roughness = 0.015;
		float density_randomization = 2.0;
	} settings;

	Texture2D* noiseTexture = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> noiseGenProgram = nullptr;

	struct GlintParameters
	{
		float _ScreenSpaceScale;
		float _LogMicrofacetDensity;
		float _MicrofacetRoughness;
		float _DensityRandomization;

		int _Glint2023NoiseMapSize;
		float pad[3];
	};
	GlintParameters GetCommonBufferData();

	virtual void SetupResources() override;
	void CompileComputeShaders();
	void GenerateNoise();

	virtual inline void Reset() override{};
	virtual void ClearShaderCache() override;

	virtual void DrawSettings() override;

	virtual void Prepass() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual inline void RestoreDefaultSettings() { settings = {}; }
};
