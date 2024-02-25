#pragma once

#include "Buffer.h"
#include "Feature.h"

struct TerrainOcclusion : public Feature
{
	static TerrainOcclusion* GetSingleton()
	{
		static TerrainOcclusion singleton;
		return std::addressof(singleton);
	}

	virtual inline std::string GetName() { return "Terrain Occlusion"; }
	virtual inline std::string GetShortName() { return "TerrainOcclusion"; }
	inline std::string_view GetShaderDefineName() override { return "TERRA_OCC"; }
	inline bool HasShaderDefine(RE::BSShader::Type) override { return true; };

	struct Settings
	{
		struct AOGenSettings
		{
			float AoDistance = 12;
			uint SliceCount = 60;
			uint SampleCount = 60;
		} AoGen;

		struct EffectSettings
		{
			uint EnableTerrainShadow = true;
			uint EnableTerrainAO = true;

			float ShadowBias = -100.f;  // in game unit
			float ShadowSofteningRadiusAngle = 2.f * RE::NI_PI / 180.f;
			float ShadowMinStep = .75f;
			// float ShadowMaxDistance = 15;
			float ShadowAnglePower = 1.f;
			uint ShadowSamples = 10;

			float AOAmbientMix = 1.f;
			float AODiffuseMix = 0.f;
			float AOPower = 1.f;
			float AOFadeOutHeight = 2000;
		} Effect;
	} settings;

	bool needPrecompute = false;

	struct HeightMapMetadata
	{
		std::wstring dir;
		std::string filename;
		std::string worldspace;
		float3 pos0, pos1;  // left-top-z=0 vs right-bottom-z=1
		float2 zRange;
	};
	std::unordered_map<std::string, HeightMapMetadata> heightmaps;
	HeightMapMetadata* cachedHeightmap;

	struct AOGenBuffer
	{
		Settings::AOGenSettings settings;

		float3 pos0;
		float3 pos1;
		float2 zRange;
	};
	std::unique_ptr<Buffer> aoGenBuffer = nullptr;

	struct PerPass
	{
		Settings::EffectSettings effect;

		float3 scale;
		float3 invScale;
		float3 offset;
		float2 zRange;

		float ShadowSofteningDiameterRcp;
		float AoDistance;
	};
	std::unique_ptr<Buffer> perPass = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> occlusionProgram = nullptr;

	std::unique_ptr<Texture2D> texHeightMap = nullptr;
	std::unique_ptr<Texture2D> texOcclusion = nullptr;
	std::unique_ptr<Texture2D> texHeightCone = nullptr;

	virtual void SetupResources() override;
	void CompileComputeShaders();

	virtual void DrawSettings() override;

	virtual void Reset() override;

	virtual void Draw(const RE::BSShader* shader, const uint32_t descriptor) override;
	void LoadHeightmap();
	void LoadPrecomputedTex();
	void Precompute();
	void ModifyLighting();

	virtual void Load(json& o_json) override;
	virtual void Save(json&) override;

	virtual inline void RestoreDefaultSettings() override { settings = {}; }
	virtual void ClearShaderCache() override;
};