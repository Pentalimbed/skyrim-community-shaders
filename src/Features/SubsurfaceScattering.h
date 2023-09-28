#pragma once

#include "Buffer.h"
#include "Feature.h"

struct SubsurfaceScattering : Feature
{
public:
	static SubsurfaceScattering* GetSingleton()
	{
		static SubsurfaceScattering singleton;
		return &singleton;
	}

	static void InstallHooks()
	{
		Hooks::Install();
	}

	struct alignas(16) BlurCB
	{
		float4 DynamicRes;
		float4 CameraData;
		float2 BufferDim;
		float2 RcpBufferDim;
		uint FrameCount;
		float SSSS_FOVY;
		float SSSWidth;
		float pad;
	};

	ConstantBuffer* blurCB = nullptr;

	struct PerPass
	{
		uint SkinMode;
		uint pad[3];
	};

	std::unique_ptr<Buffer> perPass = nullptr;

	Texture2D* specularTexture = nullptr;
	Texture2D* albedoTexture = nullptr;
	Texture2D* deferredTexture = nullptr;
	Texture2D* ambientTexture = nullptr;

	Texture2D* colorTextureTemp = nullptr;
	Texture2D* colorTextureTemp2 = nullptr;
	Texture2D* depthTextureTemp = nullptr;

	ID3D11ComputeShader* horizontalSSBlur = nullptr;
	ID3D11ComputeShader* verticalSSBlur = nullptr;

	ID3D11SamplerState* linearSampler = nullptr;
	ID3D11SamplerState* pointSampler = nullptr;

	uint skinMode = false;

	std::set<ID3D11BlendState*> mappedBlendStates;
	std::map<ID3D11BlendState*, ID3D11BlendState*> modifiedBlendStates;

	virtual inline std::string GetName() { return "Subsurface Scattering"; }
	virtual inline std::string GetShortName() { return "SubsurfaceScattering"; }

	virtual void SetupResources();
	virtual inline void Reset() {}

	virtual void DrawSettings();

	virtual void Draw(const RE::BSShader* shader, const uint32_t descriptor);

	virtual void Load(json& o_json);
	virtual void Save(json& o_json);

	void DrawDeferred();

	void ClearComputeShader();
	ID3D11ComputeShader* GetComputeShaderHorizontalBlur();
	ID3D11ComputeShader* GetComputeShaderVerticalBlur();

	void BSLightingShader_SetupGeometry_Before(RE::BSRenderPass* Pass);

	struct Hooks
	{
		struct BSLightingShader_SetupGeometry
		{
			static void thunk(RE::BSShader* This, RE::BSRenderPass* Pass, uint32_t RenderFlags)
			{
				GetSingleton()->BSLightingShader_SetupGeometry_Before(Pass);
				func(This, Pass, RenderFlags);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			stl::write_vfunc<0x6, BSLightingShader_SetupGeometry>(RE::VTABLE_BSLightingShader[0]);

			logger::info("[SSS] Installed hooks");
		}
	};
};
