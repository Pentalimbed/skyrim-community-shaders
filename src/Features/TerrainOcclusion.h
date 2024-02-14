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

	struct AOGenSettings
	{
		float AoDistance = 8;
		uint SliceCount = 30;
		uint SampleCount = 30;
	};
	struct EffectSettings
	{
		uint EnableTerrainShadow = true;
		uint EnableTerrainAO = true;

		float ShadowSoftening = 0.0872665f;  // 5 deg
		float ShadowMaxDistance = 15;
		float ShadowAnglePower = 4.f;
		uint ShadowSamples = 9;

		float AOAmbientMix = 1.f;
		float AODiffuseMix = 0.f;
		float AOPower = 1.f;
		float AOFadeOutHeight = 2000;
	};
	struct Settings
	{
		AOGenSettings aoGen;
		EffectSettings effect;
	} settings;

	bool needAoGen = false;

	struct HeightMapMetadata
	{
		std::wstring path;
		std::string worldspace;
		float3 pos0, pos1;  // left-top-z=0 vs right-bottom-z=1
	};
	std::unordered_map<std::string, HeightMapMetadata> heightmaps;
	HeightMapMetadata* cachedHeightmap;

	struct AOGenBuffer
	{
		AOGenSettings settings;

		float3 pos0;
		float3 pos1;
	};
	std::unique_ptr<Buffer> aoGenBuffer = nullptr;

	struct PerPass
	{
		EffectSettings effect;

		float3 scale;
		float3 invScale;
		float3 offset;
	};
	std::unique_ptr<Buffer> perPass = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> occlusionProgram = nullptr;

	std::unique_ptr<Texture2D> texHeightMap = nullptr;
	std::unique_ptr<Texture2D> texOcclusion = nullptr;

	virtual void SetupResources();
	void CompileComputeShaders();

	virtual void DrawSettings();

	virtual void Reset();

	virtual void Draw(const RE::BSShader* shader, const uint32_t descriptor);
	void LoadHeightmap();
	void GenerateAO();
	void ModifyLighting();

	virtual void Load(json& o_json);
	virtual inline void Save(json&) {}

	virtual inline void RestoreDefaultSettings() { settings = {}; }
	virtual void ClearShaderCache() override;
};