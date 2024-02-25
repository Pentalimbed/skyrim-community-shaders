#include "TerrainOcclusion.h"

#include "Util.h"

#include <DirectXTex.h>
#include <filesystem>

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TerrainOcclusion::Settings::AOGenSettings,
	AoDistance,
	SliceCount,
	SampleCount)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TerrainOcclusion::Settings::EffectSettings,
	EnableTerrainShadow,
	EnableTerrainAO,
	ShadowBias,
	ShadowSofteningRadiusAngle,
	ShadowMinStep,
	// ShadowMaxDistance,
	ShadowAnglePower,
	ShadowSamples,
	AOAmbientMix,
	AODiffuseMix,
	AOPower,
	AOFadeOutHeight)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	TerrainOcclusion::Settings,
	AoGen,
	Effect)

class FrameChecker
{
private:
	uint32_t last_frame = UINT32_MAX;

public:
	inline bool isNewFrame(uint32_t frame)
	{
		bool retval = last_frame != frame;
		last_frame = frame;
		return retval;
	}
	inline bool isNewFrame() { return isNewFrame(RE::BSGraphics::State::GetSingleton()->uiFrameCount); }
};

void TerrainOcclusion::Load(json& o_json)
{
	if (o_json[GetName()].is_object())
		settings = o_json[GetName()];

	Feature::Load(o_json);
}

void TerrainOcclusion::Save(json& o_json)
{
	o_json[GetName()] = settings;
}

void TerrainOcclusion::DrawSettings()
{
	ImGui::Checkbox("Enable Terrain Shadow", (bool*)&settings.Effect.EnableTerrainShadow);
	ImGui::Checkbox("Enable Terrain AO", (bool*)&settings.Effect.EnableTerrainAO);

	if (ImGui::TreeNodeEx("Shadow", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("Bias", &settings.Effect.ShadowBias, -500.f, 0.f, "%.1f units");
		ImGui::SliderAngle("Softening", &settings.Effect.ShadowSofteningRadiusAngle, .1f, 10.f, "%.2f deg", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Controls the solid angle of sunlight, making terrain shadows softer.");
		ImGui::SliderFloat("Min Stepping Distance", &settings.Effect.ShadowMinStep, 0.05, 3, "%.2f cells");
		// ImGui::SliderFloat("Max Distance", &settings.Effect.ShadowMaxDistance, 1, 30, "%.2f cells");
		ImGui::SliderFloat("Angle Exaggeration", &settings.Effect.ShadowAnglePower, 1, 8, "%.1f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Arbitarily lowers the vanilla sunlight angle that is rather high even at sunrise/sunset, making terrain shadows longer.");

		const uint sampleMin = 1u;
		const uint sampleMax = 30u;
		ImGui::SliderScalar("Samples", ImGuiDataType_U32, &settings.Effect.ShadowSamples, &sampleMin, &sampleMax);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text("Longer shadows require more samples, but softening helps cover the inaccuracy.");

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("AO ", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("Ambient Mix", &settings.Effect.AOAmbientMix, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		ImGui::SliderFloat("Diffuse Mix", &settings.Effect.AODiffuseMix, 0, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(
				"Values greater than 0 are not exactly what AMBIENT occlusion is intended to be.\n"
				"This is for people who really what that grey halo look.");

		ImGui::SliderFloat("Power", &settings.Effect.AOPower, 0.2f, 5, "%.2f");
		ImGui::SliderFloat("Fadeout Height", &settings.Effect.AOFadeOutHeight, 500, 5000, "%.0f");
		if (auto _tt = Util::HoverTooltipWrapper())
			ImGui::Text(
				"On the ground AO is the most prominent. Up to a certain height it will gradually fade out.");

		if (ImGui::TreeNodeEx("Precomputation", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderFloat("Distance", &settings.AoGen.AoDistance, 1.f / 32, 32, "%.2f cells");
			ImGui::InputScalar("Slices", ImGuiDataType_U32, &settings.AoGen.SliceCount);
			ImGui::InputScalar("Samples", ImGuiDataType_U32, &settings.AoGen.SampleCount);
			if (ImGui::Button("Force Regenerate AO", { -1, 0 }))
				needAoGen = true;

			ImGui::TreePop();
		}
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Debug")) {
		std::string curr_worldspace = "N/A";
		auto tes = RE::TES::GetSingleton();
		if (tes) {
			auto worldspace = tes->GetRuntimeData2().worldSpace;
			if (worldspace)
				curr_worldspace = worldspace->GetName();
		}
		ImGui::Text(fmt::format("Current worldspace: {}", curr_worldspace).c_str());
		ImGui::Text(fmt::format("Has height map: {}", heightmaps.contains(curr_worldspace)).c_str());

		if (texOcclusion)
			ImGui::Image(texOcclusion->srv.get(), { texOcclusion->desc.Width / 5.f, texOcclusion->desc.Height / 5.f });
		ImGui::TreePop();
	}
}

void TerrainOcclusion::ClearShaderCache()
{
	if (occlusionProgram) {
		occlusionProgram->Release();
		occlusionProgram = nullptr;
	}

	CompileComputeShaders();
}

void TerrainOcclusion::SetupResources()
{
	logger::debug("Listing height maps...");
	{
		std::filesystem::path texture_dir{ L"Data\\textures\\heightmaps\\" };
		for (auto const& dir_entry : std::filesystem::directory_iterator{ texture_dir }) {
			auto filename = dir_entry.path().filename();
			if (filename.extension() != ".dds")
				continue;

			logger::debug("Found dds: {}", filename.string());

			std::stringstream ss(filename.stem().string());
			std::string item;

			uint pos = 0;
			HeightMapMetadata metadata;
			try {
				while (getline(ss, item, '.')) {
					switch (pos) {
					case 0:
						metadata.worldspace = item;
						break;
					case 1:
						metadata.pos0.x = std::stoi(item) * 4096.f;
						break;
					case 2:
						metadata.pos1.y = std::stoi(item) * 4096.f;
						break;
					case 3:
						metadata.pos1.x = (std::stoi(item) + 1) * 4096.f;
						break;
					case 4:
						metadata.pos0.y = (std::stoi(item) + 1) * 4096.f;
						break;
					case 5:
						metadata.pos0.z = std::stoi(item) * 8.f;
						break;
					case 6:
						metadata.pos1.z = std::stoi(item) * 8.f;
						break;
					default:
						break;
					}
					pos++;
				}
			} catch (std::exception& e) {
				logger::warn("Failed to parse {}. Error: {}", filename.string(), e.what());
				continue;
			}
			metadata.path = dir_entry.path().wstring();

			if (heightmaps.contains(metadata.worldspace)) {
				logger::warn("{} has more than one height maps!", metadata.worldspace);
			} else {
				heightmaps[metadata.worldspace] = metadata;
			}
		}
	}

	logger::debug("Creating structured buffers...");
	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(AOGenBuffer);
		sbDesc.ByteWidth = sizeof(AOGenBuffer);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = 1;

		aoGenBuffer = std::make_unique<Buffer>(sbDesc);
		aoGenBuffer->CreateSRV(srvDesc);

		sbDesc.StructureByteStride = sizeof(PerPass);
		sbDesc.ByteWidth = sizeof(PerPass);

		perPass = std::make_unique<Buffer>(sbDesc);
		perPass->CreateSRV(srvDesc);
	}

	// logger::debug("Creating samplers...");
	// {
	// 	D3D11_SAMPLER_DESC samplerDesc = {};
	// 	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	// 	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	// 	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	// 	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	// 	samplerDesc.MaxAnisotropy = 1;
	// 	samplerDesc.MinLOD = 0;
	// 	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
	// 	DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, heightmapSampler.put()));
	// }

	CompileComputeShaders();
}

void TerrainOcclusion::CompileComputeShaders()
{
	logger::debug("Compiling shaders...");
	{
		auto occlusion_program_ptr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\TerrainOcclusion\\AOGen.cs.hlsl", { {} }, "cs_5_0"));
		if (occlusion_program_ptr)
			occlusionProgram.attach(occlusion_program_ptr);
	}

	needAoGen = true;
}

void TerrainOcclusion::Draw(const RE::BSShader* shader, const uint32_t)
{
	LoadHeightmap();

	if (needAoGen)
		GenerateAO();

	switch (shader->shaderType.get()) {
	case RE::BSShader::Type::Lighting:
	case RE::BSShader::Type::Grass:
	case RE::BSShader::Type::DistantTree:
		ModifyLighting();
		break;

	default:
		break;
	}
}

void TerrainOcclusion::LoadHeightmap()
{
	static FrameChecker frame_checker;
	if (!frame_checker.isNewFrame())
		return;

	auto tes = RE::TES::GetSingleton();
	if (!tes)
		return;
	auto worldspace = tes->GetRuntimeData2().worldSpace;
	if (!worldspace)
		return;
	std::string worldspace_name = worldspace->GetName();
	if (!heightmaps.contains(worldspace_name))
		return;
	if (cachedHeightmap && cachedHeightmap->worldspace == worldspace_name)
		return;

	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	auto device = renderer->GetRuntimeData().forwarder;

	logger::debug("Loading height map...");
	{
		auto& target_heightmap = heightmaps[worldspace_name];

		DirectX::ScratchImage image;
		try {
			DX::ThrowIfFailed(LoadFromDDSFile(target_heightmap.path.c_str(), DirectX::DDS_FLAGS_NONE, nullptr, image));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		ID3D11Resource* pResource = nullptr;
		try {
			DX::ThrowIfFailed(CreateTexture(device,
				image.GetImages(), image.GetImageCount(),
				image.GetMetadata(), &pResource));
		} catch (const DX::com_exception& e) {
			logger::error("{}", e.what());
			return;
		}

		texHeightMap.release();
		texHeightMap = std::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pResource));

		D3D11_TEXTURE2D_DESC texDesc{};
		texHeightMap->resource->GetDesc(&texDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texHeightMap->desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};
		texHeightMap->CreateSRV(srvDesc);

		cachedHeightmap = &heightmaps[worldspace_name];
	}

	logger::debug("Creating occlusion texture...");
	{
		texOcclusion.release();

		D3D11_TEXTURE2D_DESC texDesc = {
			.Width = texHeightMap->desc.Width,
			.Height = texHeightMap->desc.Height,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
			.SampleDesc = { .Count = 1 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS
		};

		texOcclusion = std::make_unique<Texture2D>(texDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};
		texOcclusion->CreateSRV(srvDesc);

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};
		texOcclusion->CreateUAV(uavDesc);
	}

	needAoGen = true;
}

void TerrainOcclusion::GenerateAO()
{
	if (!cachedHeightmap)
		return;

	auto context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;

	{
		AOGenBuffer data = {
			.settings = settings.AoGen,
			.pos0 = cachedHeightmap->pos0,
			.pos1 = cachedHeightmap->pos1
		};

		data.settings.AoDistance *= 4096.f;

		D3D11_MAPPED_SUBRESOURCE mapped;
		DX::ThrowIfFailed(context->Map(aoGenBuffer->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
		size_t bytes = sizeof(AOGenBuffer);
		memcpy_s(mapped.pData, bytes, &data, bytes);
		context->Unmap(aoGenBuffer->resource.get(), 0);
	}

	/* ---- BACKUP ---- */
	struct ShaderState
	{
		ID3D11ShaderResourceView* srvs[2] = { nullptr };
		ID3D11ComputeShader* shader = nullptr;
		ID3D11UnorderedAccessView* uavs[1] = { nullptr };
		ID3D11ClassInstance* instance = nullptr;
		ID3D11SamplerState* samplers[1] = { nullptr };
		UINT numInstances;
	} old, newer;
	context->CSGetShaderResources(0, ARRAYSIZE(old.srvs), old.srvs);
	context->CSGetShader(&old.shader, &old.instance, &old.numInstances);
	context->CSGetUnorderedAccessViews(0, ARRAYSIZE(old.uavs), old.uavs);
	context->CSGetSamplers(0, ARRAYSIZE(old.samplers), old.samplers);

	/* ---- DISPATCH ---- */
	logger::debug("Generating AO...");
	newer.srvs[0] = aoGenBuffer->srv.get();
	newer.srvs[1] = texHeightMap->srv.get();
	newer.uavs[0] = texOcclusion->uav.get();
	// newer.samplers[0] = heightmapSampler.get();

	context->CSSetSamplers(0, ARRAYSIZE(newer.samplers), newer.samplers);
	context->CSSetShaderResources(0, ARRAYSIZE(newer.srvs), newer.srvs);
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(newer.uavs), newer.uavs, nullptr);
	context->CSSetShader(occlusionProgram.get(), nullptr, 0);
	context->Dispatch(((texOcclusion->desc.Width - 1) >> 5) + 1, ((texOcclusion->desc.Height - 1) >> 5) + 1, 1);

	/* ---- RESTORE ---- */
	context->CSSetShaderResources(0, ARRAYSIZE(old.srvs), old.srvs);
	context->CSSetShader(old.shader, &old.instance, old.numInstances);
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(old.uavs), old.uavs, nullptr);
	context->CSSetSamplers(0, ARRAYSIZE(old.samplers), old.samplers);

	needAoGen = false;
}

void TerrainOcclusion::Reset()
{
	auto context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;

	bool isHeightmapReady = false;
	auto tes = RE::TES::GetSingleton();
	if (tes) {
		auto worldspace = tes->GetRuntimeData2().worldSpace;
		if (worldspace)
			isHeightmapReady = cachedHeightmap && cachedHeightmap->worldspace == worldspace->GetName();
	}

	PerPass data = {
		.effect = settings.Effect,
	};
	data.effect.EnableTerrainAO = data.effect.EnableTerrainAO && isHeightmapReady;
	data.effect.EnableTerrainShadow = data.effect.EnableTerrainShadow && isHeightmapReady;

	if (isHeightmapReady) {
		data.effect.ShadowMinStep *= 4096.f;

		data.effect.AOFadeOutHeight = 1.f / data.effect.AOFadeOutHeight;

		data.invScale = cachedHeightmap->pos1 - cachedHeightmap->pos0;
		data.scale = float3(1.f, 1.f, 1.f) / data.invScale;
		data.offset = -cachedHeightmap->pos0 * data.scale;

		data.ShadowSofteningDiameterRcp = .5f / data.effect.ShadowSofteningRadiusAngle;
		data.AoDistance = settings.AoGen.AoDistance * 4096.f;
	}

	D3D11_MAPPED_SUBRESOURCE mapped;
	DX::ThrowIfFailed(context->Map(perPass->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
	size_t bytes = sizeof(PerPass);
	memcpy_s(mapped.pData, bytes, &data, bytes);
	context->Unmap(perPass->resource.get(), 0);
}

void TerrainOcclusion::ModifyLighting()
{
	auto context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;

	ID3D11ShaderResourceView* srvs[2] = { nullptr };
	srvs[0] = perPass->srv.get();
	if (texOcclusion)
		srvs[1] = texOcclusion->srv.get();
	context->PSSetShaderResources(25, ARRAYSIZE(srvs), srvs);
}