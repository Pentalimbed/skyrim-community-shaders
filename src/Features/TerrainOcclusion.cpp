#include "TerrainOcclusion.h"

#include "Util.h"

#include <DirectXTex.h>

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
	Feature::Load(o_json);
}

void TerrainOcclusion::DrawSettings()
{
	ImGui::SliderFloat("AO Distance", &settings.aoGen.aoDistance, 1000, 100000, "%.1f");
	ImGui::InputScalar("Slices", ImGuiDataType_U32, &settings.aoGen.sliceCount);
	ImGui::InputScalar("Samples", ImGuiDataType_U32, &settings.aoGen.sampleCount);
	if (ImGui::Button("Regenerate AO"))
		needAoGen = true;

	ImGui::Image(texHeightMap->srv.get(), { texHeightMap->desc.Width / 10.f, texHeightMap->desc.Height / 10.f });
	ImGui::Image(texOcclusion->srv.get(), { texOcclusion->desc.Width / 10.f, texOcclusion->desc.Height / 10.f });
}

void TerrainOcclusion::ClearShaderCache()
{
	if (occlusionProgram)
		occlusionProgram->Release();

	CompileComputeShaders();
}

void TerrainOcclusion::SetupResources()
{
	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	auto device = renderer->GetRuntimeData().forwarder;

	logger::debug("Loading heightmap...");
	{
		DirectX::ScratchImage image;
		try {
			DX::ThrowIfFailed(LoadFromDDSFile(L"Data\\textures\\heightmaps\\Tamriel.HeightMap.-57.-43.61.50.-4629.4924.dds", DirectX::DDS_FLAGS_NONE, nullptr, image));
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
		texHeightMap = std::make_unique<Texture2D>(reinterpret_cast<ID3D11Texture2D*>(pResource));

		D3D11_TEXTURE2D_DESC texDesc{};
		texHeightMap->resource->GetDesc(&texDesc);

		heightMapMetadata.worldspace = "Tamriel";
		heightMapMetadata.posLU = { 61 * 4096, 50 * 4096, -4629 * 8 };
		heightMapMetadata.posRB = { -57 * 4096, -43 * 4096, 4924 * 8 };

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texHeightMap->desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};
		texHeightMap->CreateSRV(srvDesc);
	}

	logger::debug("Creating occlusion texture...");
	{
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

	logger::debug("Creating structured buffers...");
	{
		D3D11_BUFFER_DESC sbDesc{};
		sbDesc.Usage = D3D11_USAGE_DYNAMIC;
		sbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		sbDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		sbDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		sbDesc.StructureByteStride = sizeof(AOGenBuffer);
		sbDesc.ByteWidth = sizeof(AOGenBuffer);
		aoGenBuffer = std::make_unique<Buffer>(sbDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		srvDesc.Buffer.FirstElement = 0;
		srvDesc.Buffer.NumElements = 1;
		aoGenBuffer->CreateSRV(srvDesc);
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

void TerrainOcclusion::Draw(const RE::BSShader*, const uint32_t)
{
	if (needAoGen)
		GenerateAO();
}

void TerrainOcclusion::GenerateAO()
{
	auto context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;

	{
		AOGenBuffer data = {
			.settings = settings.aoGen,
			.posLU = heightMapMetadata.posLU,
			.posRB = heightMapMetadata.posRB
		};

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
