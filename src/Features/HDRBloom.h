#pragma once

#include "Buffer.h"
#include "Feature.h"

struct HDRBloom : public Feature
{
	static HDRBloom* GetSingleton()
	{
		static HDRBloom singleton;
		return std::addressof(singleton);
	}

	virtual inline std::string GetName() { return "HDR Bloom"; }
	virtual inline std::string GetShortName() { return "HDRBloom"; }
	virtual inline std::string_view GetShaderDefineName() override { return "HDR_BLOOM"; }
	virtual inline bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	float avgLum = .1f;

	struct Settings
	{
		bool EnableBloom = true;
		bool EnableTonemapper = true;

		// light power
		struct ManualLightTweaks
		{
			float DirLightPower = 2.f;
			float LightPower = 1.5f;
			float AmbientPower = 1.25f;
			float EmitPower = 2.f;
		} LightTweaks;

		// auto exposure
		float MinLogLum = -5.f;
		float MaxLogLum = 1.f;
		float AdaptSpeed = 1.5f;

		// bloom
		bool EnableNormalisation = true;

		float UpsampleRadius = 1.f;
		std::array<float, 8> MipBlendFactor = { .1f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };

		// tonemap
		struct TonemapperSettings
		{
			float Exposure = 0.38f;

			float Slope = 1.2f;
			float Power = 1.3f;
			float Offset = -0.1f;
			float Saturation = 1.2f;
		} Tonemapper;

	} settings;

	// buffers
	struct LightTweaksSB
	{
		Settings::ManualLightTweaks settings;
	};
	std::unique_ptr<StructuredBuffer> lightTweaksSB = nullptr;

	struct alignas(16) AutoExposureCB
	{
		float AdaptLerp;

		float MinLogLum;
		float LogLumRange;
		float RcpLogLumRange;

		float pad[4 - (4) % 4];
	};
	std::unique_ptr<ConstantBuffer> autoExposureCB = nullptr;

	struct alignas(16) BloomCB
	{
		uint IsFirstMip;
		float UpsampleMult;
		float NormalisationFactor;

		float UpsampleRadius;

		float pad[4 - (4) % 4];
	};
	std::unique_ptr<ConstantBuffer> bloomCB = nullptr;

	struct alignas(16) TonemapCB
	{
		Settings::TonemapperSettings settings;

		float pad[4 - (sizeof(settings) / 4) % 4];
	};
	std::unique_ptr<ConstantBuffer> tonemapCB = nullptr;

	// textures
	std::unique_ptr<Texture1D> texHistogram = nullptr;
	std::unique_ptr<Texture1D> texAdaptation = nullptr;
	std::unique_ptr<Texture2D> texBloom = nullptr;
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 9> texBloomMipSRVs = { nullptr };
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 9> texBloomMipUAVs = { nullptr };
	std::unique_ptr<Texture2D> texTonemap = nullptr;

	// programs
	winrt::com_ptr<ID3D11ComputeShader> histogramProgram = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> histogramAvgProgram = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> bloomDownsampleProgram = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> bloomUpsampleProgram = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> tonemapProgram = nullptr;

	virtual void SetupResources() override;
	void CompileComputeShaders();
	virtual void ClearShaderCache() override;

	virtual void Reset() override;

	virtual void DrawSettings() override;

	virtual void Draw(const RE::BSShader*, const uint32_t) override;

	struct ResourceInfo
	{
		ID3D11Texture2D* tex;
		ID3D11ShaderResourceView* srv;
	};
	virtual void DrawPreProcess() override;
	ResourceInfo DrawCODBloom(ResourceInfo tex_input);
	ResourceInfo DrawTonemapper(ResourceInfo tex_input);

	virtual void Load(json& o_json) override;
	virtual void Save(json& o_json) override;

	virtual inline void RestoreDefaultSettings() override { settings = {}; };
};
