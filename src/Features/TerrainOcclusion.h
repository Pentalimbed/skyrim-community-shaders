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
		float idksmth;
	} settings;

	struct alignas(16) PerPass
	{
		Settings settings;
		float pad[4 - (sizeof(Settings) / 4) % 4];
	};
	std::unique_ptr<Buffer> perPass = nullptr;

	struct HeightMapMetaData
	{
		std::string worldspace;
		float3 scale, offset;  // (uv, z) = pos * scale + offset
	} heightMapMetadata;
	std::unique_ptr<Texture2D> texHeightMap = nullptr;

	virtual void SetupResources();
	virtual inline void Reset() {}

	virtual void DrawSettings();

	virtual void Draw(const RE::BSShader* shader, const uint32_t descriptor);

	virtual void Load(json& o_json);
	virtual inline void Save(json&) {}

	virtual inline void RestoreDefaultSettings() { settings = {}; }
};