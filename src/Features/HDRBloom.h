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

	struct Settings
	{
		bool EnableBloom = true;
		bool EnableTonemapper = true;

		// bloom
		bool EnableNormalisation = true;

		float UpsampleRadius = 1.f;
		std::array<float, 8> MipBlendFactor = { .1f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f };

		// tonemap
		struct TonemapSettings
		{
			float Exposure = 0.f;

			float Slope = 1.f;
			float Power = 1.f;
			float Offset = 0.f;
			float Saturation = 1.4f;
		} Tonemapper;

	} settings;

	// constant buffers
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
		Settings::TonemapSettings settings;

		float pad[4 - (sizeof(settings) / 4) % 4];
	};
	std::unique_ptr<ConstantBuffer> tonemapCB = nullptr;

	// textures
	std::unique_ptr<Texture2D> texAdapt = nullptr;
	std::unique_ptr<Texture2D> texBloom = nullptr;
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 9> texBloomMipSRVs = { nullptr };
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 9> texBloomMipUAVs = { nullptr };
	std::unique_ptr<Texture2D> texTonemap = nullptr;

	// programs
	winrt::com_ptr<ID3D11ComputeShader> bloomDownsampleProgram = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> bloomUpsampleProgram = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> tonemapProgram = nullptr;

	virtual void SetupResources() override;
	void CompileComputeShaders();
	virtual void ClearShaderCache() override;

	virtual inline void Reset() override {}

	virtual void DrawSettings() override;

	virtual inline void Draw(const RE::BSShader*, const uint32_t) override{};

	struct ResourceInfo
	{
		ID3D11Texture2D* tex;
		ID3D11ShaderResourceView* srv;
	};
	virtual void DrawPreProcess() override;
	ResourceInfo DrawCODBloom(ResourceInfo tex_input);

	virtual void Load(json& o_json) override;
	virtual void Save(json& o_json) override;

	virtual inline void RestoreDefaultSettings() override { settings = {}; };
};
