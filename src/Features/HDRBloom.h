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

	float avgLum = .1f;

	struct Settings
	{
		// bloom
		bool EnableBloom = true;
		bool EnableNormalisation = false;

		float UpsampleRadius = 2.f;
		float BlendFactor = .1f;
		std::array<float, 8> MipBlendFactor = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };

		// auto exposure
		bool EnableAutoExposure = true;
		bool AdaptAfterBloom = true;

		float2 HistogramRange = { -5.f, 16.f };
		float2 AdaptArea = { .6f, .6f };

		float AdaptSpeed = 1.5f;

		// tonemap
		bool EnableTonemapper = true;
		struct TonemapperSettings
		{
			float2 AdaptationRange = { -1.f, 1.f };

			float KeyValue = 0.8f;
			float ExposureCompensation = 0.f;

			float Slope = 1.4f;
			float Power = 1.5f;
			float Offset = -0.3f;
			float Saturation = 1.2f;

			float PurkinjeStartEV = -1.f;
			float PurkinjeMaxEV = -4.f;
			float PurkinjeStrength = 1.f;

		} Tonemapper;

	} settings;

	// buffers
	struct alignas(16) AutoExposureCB
	{
		float AdaptLerp;

		float2 AdaptArea;

		float MinLogLum;
		float LogLumRange;
		float RcpLogLumRange;

		float pad[4 - (6) % 4];
	};
	std::unique_ptr<ConstantBuffer> autoExposureCB = nullptr;

	struct alignas(16) BloomCB
	{
		uint IsZeroMip;
		uint IsFirstMip;
		float UpsampleMult;
		float CurrentMipMult;
		float NormalisationFactor;

		float UpsampleRadius;

		float pad[4 - (6) % 4];
	};
	std::unique_ptr<ConstantBuffer> bloomCB = nullptr;

	struct alignas(16) TonemapCB
	{
		Settings::TonemapperSettings settings;

		uint EnableAutoExposure;

		float pad[4 - (sizeof(settings) / 4 + 1) % 4];
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
	void DrawAdaptation(ResourceInfo tex_input);
	ResourceInfo DrawCODBloom(ResourceInfo tex_input);
	ResourceInfo DrawTonemapper(ResourceInfo tex_input);

	virtual void Load(json& o_json) override;
	virtual void Save(json& o_json) override;

	virtual inline void RestoreDefaultSettings() override { settings = {}; };
};
