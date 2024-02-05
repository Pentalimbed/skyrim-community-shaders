#include "TerrainOcclusion.h"

#include <DirectXTex.h>

void TerrainOcclusion::Load(json& o_json)
{
	Feature::Load(o_json);
}

void TerrainOcclusion::SetupResources()
{
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
		auto device = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().forwarder;
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
		heightMapMetadata.scale = { -32 / 4096.f, -32 / 4096.f, 1 / 524288.f };
		heightMapMetadata.offset = { 61 * 4096.f, 50 * 4096.f, 32767 / 65536.f };

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texHeightMap->desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};
		texHeightMap->CreateSRV(srvDesc);
	}
}

void TerrainOcclusion::DrawSettings()
{
	ImGui::Image(texHeightMap->srv.get(), { texHeightMap->desc.Width / 10.f, texHeightMap->desc.Height / 10.f });
}

void TerrainOcclusion::Draw(const RE::BSShader*, const uint32_t)
{
}
