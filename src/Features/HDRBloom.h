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

	constexpr static size_t s_BloomMips = 9;

	struct GhostParameters
	{
		uint Mip = 1;
		float Scale = 1.f;
		float Intensity = 0.f;
		float Chromatic = 0.f;
	};

	struct Settings
	{
		// bloom
		bool EnableBloom = true;
		bool EnableGhosts = false;

		float BloomThreshold = -6.f;  // EV
		float BloomUpsampleRadius = 2.f;
		float BloomBlendFactor = .1f;
		std::array<float, s_BloomMips - 1> MipBloomBlendFactor = { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };

		float GhostsThreshold = 1.f;  // EV
		std::array<GhostParameters, s_BloomMips> GhostParams = {};

		// tonemap
		bool EnableTonemapper = true;

		float ExposureCompensation = 2.f;

		// auto exposure
		bool EnableAutoExposure = true;
		bool AdaptAfterBloom = true;

		float2 HistogramRange = { -5.f, 16.f };  // EV
		float2 AdaptationRange = { -1.f, 1.f };  // EV
		float2 AdaptArea = { .6f, .6f };

		float AdaptSpeed = 1.5f;

		float Slope = 1.3f;
		float Power = 1.5f;
		float Offset = -0.25f;
		float Saturation = 1.2f;

		float PurkinjeStartEV = -1.f;  // EV
		float PurkinjeMaxEV = -4.f;    // EV
		float PurkinjeStrength = 1.f;

		// dither
		int DitherMode = 1;
	} settings;

	// buffers
	struct alignas(16) AutoExposureCB
	{
		float AdaptLerp;

		float2 AdaptArea;

		float MinLogLum;
		float LogLumRange;
		float RcpLogLumRange;

		float pad[2];
	};
	std::unique_ptr<ConstantBuffer> autoExposureCB = nullptr;

	struct alignas(16) BloomCB
	{
		// threshold
		float2 Thresholds;
		// downsample
		uint IsFirstMip;
		// upsample
		float UpsampleRadius;
		float UpsampleMult;    // in composite: bloom mult
		float CurrentMipMult;  // in composite: ghosts mult

		float pad[2];
	};
	std::unique_ptr<ConstantBuffer> bloomCB = nullptr;

	std::unique_ptr<StructuredBuffer> ghostsSB = nullptr;

	struct alignas(16) TonemapCB
	{
		float2 AdaptationRange;

		float ExposureCompensation;

		float Slope;
		float Power;
		float Offset;
		float Saturation;

		float PurkinjeStartEV;
		float PurkinjeMaxEV;
		float PurkinjeStrength;

		uint EnableAutoExposure;
		uint DitherMode;

		float Timer;

		float pad[3];
	};
	std::unique_ptr<ConstantBuffer> tonemapCB = nullptr;

	// textures
	std::unique_ptr<Texture1D> texHistogram = nullptr;
	std::unique_ptr<Texture1D> texAdaptation = nullptr;
	std::unique_ptr<Texture2D> texBloom = nullptr;
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, s_BloomMips> texBloomMipSRVs = { nullptr };
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, s_BloomMips> texBloomMipUAVs = { nullptr };
	std::unique_ptr<Texture2D> texGhostsBlur = nullptr;
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, s_BloomMips> texGhostsMipSRVs = { nullptr };
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, s_BloomMips> texGhostsMipUAVs = { nullptr };
	std::unique_ptr<Texture2D> texGhosts = nullptr;
	std::unique_ptr<Texture2D> texTonemap = nullptr;

	// samplers
	winrt::com_ptr<ID3D11SamplerState> colorSampler = nullptr;

	// programs
	winrt::com_ptr<ID3D11ComputeShader> histogramProgram = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> histogramAvgProgram = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> bloomThresholdProgram = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> bloomDownsampleProgram[3] = { nullptr };  // 0 - bloom only, 1 - ghost only, 2 - both
	winrt::com_ptr<ID3D11ComputeShader> bloomUpsampleProgram = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> bloomGhostsProgram = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> bloomCompositeProgram = nullptr;
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
