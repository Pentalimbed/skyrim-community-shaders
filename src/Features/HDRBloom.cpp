#include "HDRBloom.h"

#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	HDRBloom::Settings,
	EnableBloom,
	EnableTonemapper,
	UpsampleRadius,
	MipBlendFactor)

void HDRBloom::DrawSettings()
{
	ImGui::Checkbox("Enable Bloom", &settings.EnableBloom);
	ImGui::Checkbox("Enable Tonemapper", &settings.EnableTonemapper);

	if (ImGui::TreeNodeEx("Bloom", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Normalisation", &settings.EnableNormalisation);

		ImGui::SliderFloat("Upsampling Radius", &settings.UpsampleRadius, 1.f, 5.f, "%.1f px");
		if (ImGui::TreeNodeEx("Blend Factors", ImGuiTreeNodeFlags_DefaultOpen)) {
			for (int i = 0; i < settings.MipBlendFactor.size(); i++)
				ImGui::SliderFloat(fmt::format("Level {}", i).c_str(), &settings.MipBlendFactor[i], 0.f, 1.f, "%.2f");
			ImGui::TreePop();
		}
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Tonemapper", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("Exposure", &settings.Tonemapper.Exposure, -3.f, 3.f, "%.2f EV");
		if (ImGui::TreeNodeEx("AgX", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::SliderFloat("Slope", &settings.Tonemapper.Slope, 0.f, 2.f, "%.2f");
			ImGui::SliderFloat("Power", &settings.Tonemapper.Power, 0.f, 2.f, "%.2f");
			ImGui::SliderFloat("Offset", &settings.Tonemapper.Offset, -1.f, 1.f, "%.2f");
			ImGui::SliderFloat("Saturation", &settings.Tonemapper.Saturation, 0.f, 2.f, "%.2f");
			ImGui::TreePop();
		}
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::BulletText("texAdapt");
		ImGui::Image(texAdapt->srv.get(), { texAdapt->desc.Width * .2f, texAdapt->desc.Height * .2f });

		ImGui::BulletText("texBloom");
		static int mip = 0;
		ImGui::SliderInt("Mip Level", &mip, 0, 8, "%d", ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp);
		ImGui::Image(texBloomMipSRVs[mip].get(), { texBloom->desc.Width * .2f, texBloom->desc.Height * .2f });

		ImGui::TreePop();
	}
}

void HDRBloom::Load(json& o_json)
{
	if (o_json[GetName()].is_object())
		settings = o_json[GetName()];

	Feature::Load(o_json);
}

void HDRBloom::Save(json& o_json)
{
	o_json[GetName()] = settings;
}

void HDRBloom::SetupResources()
{
	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	ID3D11Device* device = renderer->GetRuntimeData().forwarder;

	logger::debug("Creating constant buffers...");
	{
		bloomCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<BloomCB>());
		tonemapCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<TonemapCB>());
	}

	logger::debug("Creating textures...");
	{
		// texAdapt for adaptation
		auto gameTexMainCopy = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY];

		D3D11_TEXTURE2D_DESC texDesc;
		gameTexMainCopy.texture->GetDesc(&texDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = (UINT)-1 }
		};

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		{
			texDesc.MipLevels = 0;
			texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
			texDesc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

			texAdapt = std::make_unique<Texture2D>(texDesc);
			texAdapt->CreateSRV(srvDesc);
		}

		// texTonemap
		{
			texDesc.MipLevels = 1;
			texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			texDesc.MiscFlags = 0;

			srvDesc.Texture2D.MipLevels = 1;

			texTonemap = std::make_unique<Texture2D>(texDesc);
			texTonemap->CreateSRV(srvDesc);
			texTonemap->CreateUAV(uavDesc);
		}

		// texBloom
		{
			texDesc.MipLevels = 9;

			texBloom = std::make_unique<Texture2D>(texDesc);

			// SRV for each mip
			for (uint i = 0; i < 9; i++) {
				D3D11_SHADER_RESOURCE_VIEW_DESC mipSrvDesc = {
					.Format = texDesc.Format,
					.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
					.Texture2D = {
						.MostDetailedMip = i,
						.MipLevels = 1 }
				};
				DX::ThrowIfFailed(device->CreateShaderResourceView(texBloom->resource.get(), &mipSrvDesc, texBloomMipSRVs[i].put()));
			}

			// UAV for each mip
			for (uint i = 0; i < 9; i++) {
				D3D11_UNORDERED_ACCESS_VIEW_DESC mipUavDesc = {
					.Format = texDesc.Format,
					.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
					.Texture2D = { .MipSlice = i }
				};
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texBloom->resource.get(), &mipUavDesc, texBloomMipUAVs[i].put()));
			}
		}
	}

	CompileComputeShaders();
}

void HDRBloom::CompileComputeShaders()
{
	auto programPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\HDRBloom\\bloom.cs.hlsl", { { "DOWNSAMPLE", "" } }, "cs_5_0"));
	if (programPtr)
		bloomDownsampleProgram.attach(programPtr);

	programPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\HDRBloom\\bloom.cs.hlsl", {}, "cs_5_0"));
	if (programPtr)
		bloomUpsampleProgram.attach(programPtr);

	programPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\HDRBloom\\tonemap.cs.hlsl", {}, "cs_5_0"));
	if (programPtr)
		tonemapProgram.attach(programPtr);
}

void HDRBloom::ClearShaderCache()
{
	if (bloomDownsampleProgram) {
		bloomDownsampleProgram->Release();
		bloomDownsampleProgram.detach();
	}
	if (bloomUpsampleProgram) {
		bloomUpsampleProgram->Release();
		bloomUpsampleProgram.detach();
	}
	if (tonemapProgram) {
		tonemapProgram->Release();
		tonemapProgram.detach();
	}
	CompileComputeShaders();
}

void HDRBloom::DrawPreProcess()
{
	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	auto context = renderer->GetRuntimeData().context;

	auto gameTexMain = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	ResourceInfo lastTexColor = { gameTexMain.texture, gameTexMain.SRV };

	// Adaptation mip gen
	{
		context->CopySubresourceRegion(texAdapt->resource.get(), 0, 0, 0, 0, lastTexColor.tex, 0, nullptr);
		context->GenerateMips(texAdapt->srv.get());

		lastTexColor = { texAdapt->resource.get(), texAdapt->srv.get() };
	}

	// COD Bloom
	if (settings.EnableBloom)
		lastTexColor = DrawCODBloom(lastTexColor);

	// AgX tonemap
	if (settings.EnableTonemapper) {
		// update cb
		TonemapCB cbData = {
			.settings = settings.Tonemapper
		};
		cbData.settings.Exposure = exp2(cbData.settings.Exposure);
		tonemapCB->Update(cbData);

		ID3D11ShaderResourceView* srv = lastTexColor.srv;
		ID3D11UnorderedAccessView* uav = texTonemap->uav.get();
		ID3D11Buffer* cb = tonemapCB->CB();
		context->CSSetConstantBuffers(0, 1, &cb);
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShaderResources(0, 1, &srv);
		context->CSSetShader(tonemapProgram.get(), nullptr, 0);

		context->Dispatch(((texTonemap->desc.Width - 1) >> 5) + 1, ((texTonemap->desc.Height - 1) >> 5) + 1, 1);

		// clean up
		srv = nullptr;
		uav = nullptr;
		cb = nullptr;
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShaderResources(0, 1, &srv);
		context->CSSetConstantBuffers(0, 1, &cb);
		context->CSSetShader(nullptr, nullptr, 0);

		lastTexColor = { texTonemap->resource.get(), texTonemap->srv.get() };
	}

	// either MAIN_COPY or MAIN is used as input for HDR pass
	// so we copy to both so whatever the game wants we're not failing it
	context->CopySubresourceRegion(gameTexMain.texture, 0, 0, 0, 0, lastTexColor.tex, 0, nullptr);
	context->CopySubresourceRegion(renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY].texture, 0, 0, 0, 0, lastTexColor.tex, 0, nullptr);
}

HDRBloom::ResourceInfo HDRBloom::DrawCODBloom(HDRBloom::ResourceInfo input)
{
	auto context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;

	// update cb
	BloomCB cbData = {
		.IsFirstMip = true,
		.UpsampleMult = 1.f,
		.UpsampleRadius = settings.UpsampleRadius,
	};
	bloomCB->Update(cbData);

	// copy to lowest mip
	// TO BE CHANGED
	context->CopySubresourceRegion(texBloom->resource.get(), 0, 0, 0, 0, input.tex, 0, nullptr);

	ID3D11ShaderResourceView* srv = nullptr;
	ID3D11UnorderedAccessView* uav = nullptr;
	ID3D11Buffer* cb = bloomCB->CB();
	context->CSSetConstantBuffers(0, 1, &cb);

	// downsample
	context->CSSetShader(bloomDownsampleProgram.get(), nullptr, 0);
	for (int i = 1; i < 9; i++) {
		if (settings.MipBlendFactor[i - 1] < 1e-3f)
			break;

		if (i == 2) {
			cbData.IsFirstMip = false;
			bloomCB->Update(cbData);
		}

		srv = texBloomMipSRVs[i - 1].get();
		uav = texBloomMipUAVs[i].get();
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShaderResources(0, 1, &srv);

		uint mipWidth = texBloom->desc.Width >> i;
		uint mipHeight = texBloom->desc.Height >> i;
		context->Dispatch(((mipWidth - 1) >> 5) + 1, ((mipHeight - 1) >> 5) + 1, 1);
	}

	// clear oh dear
	srv = nullptr;
	uav = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	context->CSSetShaderResources(0, 1, &srv);

	// upsample
	context->CSSetShader(bloomUpsampleProgram.get(), nullptr, 0);
	for (int i = 7; i >= 0; i--) {
		if (settings.MipBlendFactor[i] < 1e-3f)
			continue;

		if (i == 0) {
			cbData.IsFirstMip = true;

			if (settings.EnableNormalisation) {
				float normalisationFactor = 0.f;
				for (int j = 7; j >= 0; j--)
					normalisationFactor = 1 + normalisationFactor * settings.MipBlendFactor[j];
				normalisationFactor = 1.f / normalisationFactor;
				cbData.NormalisationFactor = normalisationFactor;
			} else
				cbData.NormalisationFactor = 1.f;
		}

		cbData.UpsampleMult = settings.MipBlendFactor[i];
		bloomCB->Update(cbData);

		srv = texBloomMipSRVs[i + 1].get();
		uav = texBloomMipUAVs[i].get();
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShaderResources(0, 1, &srv);

		uint mipWidth = texBloom->desc.Width >> i;
		uint mipHeight = texBloom->desc.Height >> i;
		context->Dispatch(((mipWidth - 1) >> 5) + 1, ((mipHeight - 1) >> 5) + 1, 1);
	}

	// clean up
	srv = nullptr;
	uav = nullptr;
	cb = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	context->CSSetShaderResources(0, 1, &srv);
	context->CSSetConstantBuffers(0, 1, &cb);
	context->CSSetShader(nullptr, nullptr, 0);

	return { texBloom->resource.get(), texBloomMipSRVs[0].get() };
}