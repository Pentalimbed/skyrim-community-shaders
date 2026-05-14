#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "Feature.h"

struct AdvancedWeather : public Feature
{
	////////////////////////////////////////////////// Boilerplate
	// Metadata
	virtual inline std::string GetName() override { return "Advanced Weather"; }
	virtual inline std::string GetShortName() override { return "AdvancedWeather"; }
	virtual inline std::string_view GetCategory() const override { return "Sky"; }
	virtual inline std::string GetFeatureModLink() override { return MakeNexusModURL("999999"); }
	virtual inline std::pair<std::string, std::vector<std::string>> GetFeatureSummary() override
	{
		return {
			"Enhanced weather simulation with dynamic atmospheric effects.",
			{
				"Dynamic weather patterns",
				"Enhanced precipitation effects",
				"Atmospheric scattering",
			}
		};
	}

	// Functionality
	virtual bool inline SupportsVR() override { return true; }
	virtual inline std::string_view GetShaderDefineName() override { return "ADVANCED_WEATHER"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type t) override { return t == RE::BSShader::Type::Sky; };

	// Settings & UI
	virtual void RestoreDefaultSettings() override;
	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;
	virtual void DrawSettings() override;

	// Resources
	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileShaders();

	////////////////////////////////////////////////// Feature Specific Data
	struct Settings
	{
		float cloudDensity = 0.5f;
		float3 cloudColor = { 0.8f, 0.8f, 0.8f };
		float precipitationIntensity = 0.3f;
	} settings;

	struct CbData
	{
		float cloudDensity;
		float precipitationIntensity;
		float2 _pad0;  // Padding to align to 16 bytes
		float3 cloudColor;
		float _pad1;
	};
	static_assert(sizeof(CbData) % 16 == 0);

	eastl::unique_ptr<ConstantBuffer> weatherCb = nullptr;
};
