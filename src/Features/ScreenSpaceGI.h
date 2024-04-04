#pragma once

#include "Buffer.h"
#include "Feature.h"

struct ScreenSpaceGI : Feature
{
	static ScreenSpaceGI* GetSingleton()
	{
		static ScreenSpaceGI singleton;
		return &singleton;
	}

	/////////////////////////////////////////////////////

	struct Settings
	{
		bool Enabled = true;

		float DepthRejection = 8.f;
	} settings;

	struct alignas(16) SSGICB
	{
		float DepthRejection;
		int Range;
		float Spread;
		float RcpRangeSpreadSqr;

		DirectX::XMUINT2 BufferDim;
		float2 RcpBufferDim;

		float ZFar;
		float ZNear;
		float2 DepthUnpackConsts;

		float4x4 ViewMatrix;
		float4x4 InvViewProjMatrix;
	};
	eastl::unique_ptr<ConstantBuffer> ssgiCB;

	eastl::unique_ptr<Texture2D> texArrayAtlasDepth;
	eastl::unique_ptr<Texture2D> texArrayAtlasColor;
	eastl::unique_ptr<Texture2D> texDepth;
	eastl::unique_ptr<Texture2D> texNormal;
	eastl::unique_ptr<Texture2D> texDiffuse;
	eastl::unique_ptr<Texture2D> texOutput;
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 4> srvsArrayAtlasDepth;
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 4> srvsArrayAtlasColor;
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 4> srvsDepth;
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 4> srvsNormal;
	std::array<winrt::com_ptr<ID3D11ShaderResourceView>, 4> srvsDiffuse;
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 4> uavsArrayAtlasDepth;
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 4> uavsArrayAtlasColor;
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 4> uavsDepth;
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 4> uavsNormal;
	std::array<winrt::com_ptr<ID3D11UnorderedAccessView>, 4> uavsDiffuse;

	winrt::com_ptr<ID3D11SamplerState> samplerLinearClamp;
	winrt::com_ptr<ID3D11SamplerState> samplerPointClamp;

	winrt::com_ptr<ID3D11ComputeShader> deinterleaveAtlasCS;
	winrt::com_ptr<ID3D11ComputeShader> deinterleaveRegularCS;
	winrt::com_ptr<ID3D11ComputeShader> giCS;
	winrt::com_ptr<ID3D11ComputeShader> giWideCS;
	winrt::com_ptr<ID3D11ComputeShader> upsampleCS;

	/////////////////////////////////////////////////////

	virtual inline std::string GetName() override { return "Screen-Space GI"; }
	virtual inline std::string GetShortName() override { return "ScreenSpaceGI"; }

	virtual void Load(json& o_json) override;
	virtual void Save(json& o_json) override;

	virtual void RestoreDefaultSettings() override;
	virtual void DrawSettings() override;

	virtual void SetupResources() override;
	void CompileComputeShaders();

	virtual void ClearShaderCache() override;

	virtual inline void Reset(){};

	virtual inline void Draw(const RE::BSShader*, const uint32_t) override{};

	virtual void DrawGI();
	SSGICB GetBaseCBData();

	inline bool ShadersOK() { return deinterleaveAtlasCS && deinterleaveRegularCS && giCS && giWideCS && upsampleCS; }
};