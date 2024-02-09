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

	virtual inline std::string GetName() { return "Snow Sparkles"; }
	virtual inline std::string GetShortName() { return "SnowSparkles"; }
	virtual inline std::string_view GetShaderDefineName() { return "SNOW_SPARKLES"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type t) { return t == RE::BSShader::Type::Lighting; }

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
		int _Glint2023NoiseMapSize;
		float _ScreenSpaceScale;
		float _LogMicrofacetDensity;
		float _MicrofacetRoughness;
		float _DensityRandomization;
	};
	std::unique_ptr<Buffer> glintSB = nullptr;

	virtual void SetupResources();
	void CompileComputeShaders();
	void GenerateNoise();

	virtual inline void Reset(){};
	virtual void ClearShaderCache();

	virtual void DrawSettings();

	virtual void Draw(const RE::BSShader* shader, const uint32_t descriptor);
	void ModifyLighting(const RE::BSShader* shader, const uint32_t descriptor);

	virtual void Load(json& o_json);
	virtual void Save(json& o_json);
	virtual inline void RestoreDefaultSettings() { settings = {}; }
};
