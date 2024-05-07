#pragma once

#include "Buffer.h"
#include "State.h"
#include <Features/SKylighting.h>
#include <State.h>
#include <Util.h>

#define ALBEDO RE::RENDER_TARGETS::kINDIRECT
#define SPECULAR RE::RENDER_TARGETS::kINDIRECT_DOWNSCALED
#define REFLECTANCE RE::RENDER_TARGETS::kRAWINDIRECT
#define NORMALROUGHNESS RE::RENDER_TARGETS::kRAWINDIRECT_DOWNSCALED
#define MASKS RE::RENDER_TARGETS::kRAWINDIRECT_PREVIOUS
#define CLOUDSHADOWS RE::RENDER_TARGETS::kRAWINDIRECT_PREVIOUS_DOWNSCALED

class Deferred
{
public:
	static Deferred* GetSingleton()
	{
		static Deferred singleton;
		return &singleton;
	}

	void DepthStencilStateSetDepthMode(RE::BSGraphics::DepthStencilDepthMode a_mode);

	void AlphaBlendStateSetMode(uint32_t a_mode);
	void AlphaBlendStateSetAlphaToCoverage(uint32_t a_value);
	void AlphaBlendStateSetWriteMode(uint32_t a_value);

	void SetupResources();
	void Reset();

	void StartDeferred();
	void OverrideBlendStates();
	void ResetBlendStates();
	void DeferredPasses();
	void EndDeferred();

	ID3D11BlendState* deferredBlendStates[7];
	ID3D11BlendState* forwardBlendStates[7];
	RE::RENDER_TARGET forwardRenderTargets[4];

	ID3D11ComputeShader* directionalShadowCS = nullptr;
	ID3D11ComputeShader* directionalCS = nullptr;
	ID3D11ComputeShader* ambientCompositeCS = nullptr;
	ID3D11ComputeShader* mainCompositeCS = nullptr;

	std::unordered_set<std::string> perms;
	void UpdatePerms();

	void ClearShaderCache();
	ID3D11ComputeShader* GetComputeAmbientComposite();
	ID3D11ComputeShader* GetComputeMainComposite();
	ID3D11ComputeShader* GetComputeDirectionalShadow();
	ID3D11ComputeShader* GetComputeDirectional();

	bool inWorld = false;
	bool deferredPass = false;

	struct alignas(16) DeferredCB
	{
		float4 CamPosAdjust[2];
		float4 DirLightDirectionVS[2];
		float4 DirLightColor;
		float4 CameraData;
		float2 BufferDim;
		float2 RcpBufferDim;
		DirectX::XMFLOAT4X4 ViewMatrix[2];
		DirectX::XMFLOAT4X4 ProjMatrix[2];
		DirectX::XMFLOAT4X4 ViewProjMatrix[2];
		DirectX::XMFLOAT4X4 InvViewMatrix[2];
		DirectX::XMFLOAT4X4 InvProjMatrix[2];
		DirectX::XMFLOAT4X4 InvViewProjMatrix[2];
		DirectX::XMFLOAT3X4 DirectionalAmbient;
		uint FrameCount;
		uint pad0[3];
	};

	ConstantBuffer* deferredCB = nullptr;

	ID3D11SamplerState* linearSampler = nullptr;

	Texture2D* diffuseAmbientTexture = nullptr;
	Texture2D* prevDiffuseAmbientTexture = nullptr;

	struct BSParticleShaderRainEmitter
	{
		void* vftable_BSParticleShaderRainEmitter_0;
		char _pad_8[4056];
	};

	static void Precipitation_SetupMask(RE::Precipitation* a_This)
	{
		using func_t = decltype(&Precipitation_SetupMask);
		REL::Relocation<func_t> func{ REL::RelocationID(25641, 26183) };
		func(a_This);
	}

	static void Precipitation_RenderMask(RE::Precipitation* a_This, BSParticleShaderRainEmitter* a_emitter)
	{
		using func_t = decltype(&Precipitation_RenderMask);
		REL::Relocation<func_t> func{ REL::RelocationID(25642, 26184) };
		func(a_This, a_emitter);
	}

	void UpdateConstantBuffer();

	Texture2D* occlusionTexture = nullptr;

	bool doOcclusion = true;

	struct Hooks
	{
		struct Main_Precipitation_RenderOcclusion
		{
			static void thunk()
			{
				State::GetSingleton()->BeginPerfEvent("PRECIPITATION MASK");
				State::GetSingleton()->SetPerfMarker("PRECIPITATION MASK");

				static bool doPrecip = false;

				auto sky = RE::Sky::GetSingleton();
				auto precip = sky->precip;

				if (Deferred::GetSingleton()->doOcclusion) {
					if (doPrecip) {
						doPrecip = false;

						auto precipObject = precip->currentPrecip;
						if (!precipObject) {
							precipObject = precip->lastPrecip;
						}
						if (precipObject) {
							Precipitation_SetupMask(precip);
							Precipitation_SetupMask(precip);  // Calling setup twice fixes an issue when it is raining
							auto effect = precipObject->GetGeometryRuntimeData().properties[RE::BSGeometry::States::kEffect];
							auto shaderProp = netimmerse_cast<RE::BSShaderProperty*>(effect.get());
							auto particleShaderProperty = netimmerse_cast<RE::BSParticleShaderProperty*>(shaderProp);
							auto rain = (BSParticleShaderRainEmitter*)(particleShaderProperty->particleEmitter);

							Precipitation_RenderMask(precip, rain);
						}

					} else {
						doPrecip = true;

						auto renderer = RE::BSGraphics::Renderer::GetSingleton();
						auto& precipitation = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPRECIPITATION_OCCLUSION_MAP];
						RE::BSGraphics::DepthStencilData precipitationCopy = precipitation;

						precipitation.depthSRV = Deferred::GetSingleton()->occlusionTexture->srv.get();
						precipitation.texture = Deferred::GetSingleton()->occlusionTexture->resource.get();
						precipitation.views[0] = Deferred::GetSingleton()->occlusionTexture->dsv.get();

						static float& PrecipitationShaderCubeSize = (*(float*)REL::RelocationID(515451, 401590).address());
						float originalPrecipitationShaderCubeSize = PrecipitationShaderCubeSize;

						static RE::NiPoint3& PrecipitationShaderDirection = (*(RE::NiPoint3*)REL::RelocationID(515509, 401648).address());
						RE::NiPoint3 originalParticleShaderDirection = PrecipitationShaderDirection;

						Skylighting::GetSingleton()->inOcclusion = true;
						PrecipitationShaderCubeSize = Skylighting::GetSingleton()->occlusionDistance;

						float originaLastCubeSize = precip->lastCubeSize;
						precip->lastCubeSize = PrecipitationShaderCubeSize;

						PrecipitationShaderDirection = { 0, 0, -1 };

						Precipitation_SetupMask(precip);
						Precipitation_SetupMask(precip);  // Calling setup twice fixes an issue when it is raining

						BSParticleShaderRainEmitter* rain = new BSParticleShaderRainEmitter;
						Precipitation_RenderMask(precip, rain);
						Skylighting::GetSingleton()->inOcclusion = false;
						RE::BSParticleShaderCubeEmitter* cube = (RE::BSParticleShaderCubeEmitter*)rain;
						Skylighting::GetSingleton()->viewProjMat = cube->occlusionProjection;

						cube = nullptr;
						delete rain;

						PrecipitationShaderCubeSize = originalPrecipitationShaderCubeSize;
						precip->lastCubeSize = originaLastCubeSize;

						PrecipitationShaderDirection = originalParticleShaderDirection;

						precipitation = precipitationCopy;
					}
				}
				State::GetSingleton()->EndPerfEvent();
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderWorld
		{
			static void thunk(bool a1)
			{
				GetSingleton()->inWorld = true;
				func(a1);
			}

			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderWorld_Start
		{
			static void thunk(RE::BSBatchRenderer* This, uint32_t StartRange, uint32_t EndRanges, uint32_t RenderFlags, int GeometryGroup)
			{
				// Here is where the first opaque objects start rendering
				GetSingleton()->OverrideBlendStates();
				GetSingleton()->StartDeferred();
				func(This, StartRange, EndRanges, RenderFlags, GeometryGroup);  // RenderBatches
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderWorld_End
		{
			static void thunk(RE::BSShaderAccumulator* This, uint32_t RenderFlags)
			{
				func(This, RenderFlags);
				// After this point, water starts rendering
				GetSingleton()->ResetBlendStates();
				GetSingleton()->EndDeferred();
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct Main_RenderWorld_End_Decals
		{
			static void thunk(RE::BSShaderAccumulator* This, uint32_t RenderFlags)
			{
				GetSingleton()->ResetBlendStates();
				GetSingleton()->EndDeferred();
				// After this point, decals start rendering
				func(This, RenderFlags);
			}
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			stl::write_thunk_call<Main_Precipitation_RenderOcclusion>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x3A1, 0x3A1));
			stl::write_thunk_call<Main_RenderWorld>(REL::RelocationID(35560, 36559).address() + REL::Relocate(0x831, 0x841, 0x791));
			stl::write_thunk_call<Main_RenderWorld_Start>(REL::RelocationID(99938, 106583).address() + REL::Relocate(0x8E, 0x84));
			stl::write_thunk_call<Main_RenderWorld_End>(REL::RelocationID(99938, 106583).address() + REL::Relocate(0x319, 0x308, 0x321));
			//stl::write_thunk_call<Main_RenderWorld_End>(REL::RelocationID(99938, 106583).address() + REL::Relocate(0x2F2, 0x2E1, 0x321));

			logger::info("[Deferred] Installed hooks");
		}
	};
};