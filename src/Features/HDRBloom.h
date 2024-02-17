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
		bool enableBloom = true;

		float upsampleRadius = 1.f;
		std::array<float, 8> mipBlendFactor = { .1f, 1.f, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f };
	} settings;

	struct alignas(16) BloomCB
	{
		uint isFirstDownsamplePass;
		float upsampleMult;

		float upsampleRadius;

		float pad[4 - (3) % 4];
	};
	std::unique_ptr<ConstantBuffer> bloomCB = nullptr;

	std::unique_ptr<Texture2D> texAdapt = nullptr;
	std::unique_ptr<Texture2D> texBloom = nullptr;
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 9> texBloomMipSRVs = { nullptr };
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 9> texBloomMipUAVs = { nullptr };

	winrt::com_ptr<ID3D11ComputeShader> bloomDownsampleProgram = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> bloomUpsampleProgram = nullptr;

	virtual void SetupResources() override;
	void CompileComputeShaders();
	virtual void ClearShaderCache() override;

	virtual inline void Reset() override {}

	virtual void DrawSettings() override;

	virtual inline void Draw(const RE::BSShader*, const uint32_t) override{};
	virtual void DrawPreProcess() override;
	ID3D11Texture2D* DrawCODBloom(ID3D11Texture2D* tex_input);

	virtual void Load(json& o_json) override;
	virtual void Save(json& o_json) override;

	virtual inline void RestoreDefaultSettings() override { settings = {}; };
};
