#include "HDRBloom.h"

#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	HDRBloom::Settings::TonemapperSettings,
	KeyValue,
	ExposureCompensation,
	Slope,
	Power,
	Offset,
	Saturation,
	PurkinjeStartEV,
	PurkinjeMaxEV,
	PurkinjeStrength)

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	HDRBloom::Settings,
	EnableBloom,
	EnableNormalisation,
	UpsampleRadius,
	MipBlendFactor,
	EnableAutoExposure,
	AdaptAfterBloom,
	EnableTonemapper,
	HistogramRange,
	AdaptArea,
	AdaptSpeed,
	Tonemapper)

void HDRBloom::DrawSettings()
{
	if (ImGui::BeginTabBar("##HDRBLOOM")) {
		if (ImGui::BeginTabItem("Bloom")) {
			ImGui::Checkbox("Enable Bloom", &settings.EnableBloom);

			ImGui::Checkbox("Normalisation", &settings.EnableNormalisation);
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text(
					"Prevent bloom from beightening up the image.\n"
					"Can turn off if Adapt After Bloom is on.");

			ImGui::SliderFloat("Upsampling Radius", &settings.UpsampleRadius, 1.f, 5.f, "%.1f px");
			if (ImGui::TreeNodeEx("Blend Factors", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::SliderFloat("Global", &settings.BlendFactor, 0.f, 1.f, "%.2f");
				ImGui::SeparatorText("Per Level");
				for (int i = 0; i < settings.MipBlendFactor.size(); i++) {
					ImGui::SliderFloat(fmt::format("Level {}", i).c_str(), &settings.MipBlendFactor[i], 0.f, 1.f, "%.2f");
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::Text("The greater the level, the blurrier it gets.");
				}
				ImGui::TreePop();
			}

			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Tonemapper")) {
			ImGui::Checkbox("Enable Tonemapper", &settings.EnableTonemapper);

			ImGui::SliderFloat("Key Value", &settings.Tonemapper.KeyValue, 0.f, 2.f, "%.2f");
			ImGui::SliderFloat("Exposure Compensation", &settings.Tonemapper.ExposureCompensation, -6.f, 21.f, "%.2f EV");

			if (ImGui::TreeNodeEx("Auto Exposure", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::Checkbox("Enable Auto Exposure", &settings.EnableAutoExposure);
				ImGui::Checkbox("Adapt After Bloom", &settings.AdaptAfterBloom);

				ImGui::SliderFloat("Adaptation Speed", &settings.AdaptSpeed, 0.1f, 5.f, "%.2f");
				ImGui::SliderFloat2("Focus Area", &settings.AdaptArea.x, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text("Specifies as proportions the width and height of the area that auto exposure will adapt to.");
				ImGui::SliderFloat2("Adaptation Range", &settings.Tonemapper.AdaptationRange.x, -6.f, 21.f, "%.2f EV");
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text("The average scene luminance will be clamped between them when doing auto exposure.");
				ImGui::SliderFloat2("Histogram Range", &settings.HistogramRange.x, -6.f, 21.f, "%.2f EV");

				if (ImGui::TreeNodeEx("Purkinje Effect", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::TextWrapped("The Purkinje effect simulates the blue shift of human vision under low light.");

					ImGui::SliderFloat("Max Strength", &settings.Tonemapper.PurkinjeStrength, 0.1f, 5.f, "%.2f");
					ImGui::SliderFloat("Fade In EV", &settings.Tonemapper.PurkinjeStartEV, -6.f, 21.f, "%.2f EV");
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::Text("The Purkinje effect will start to take place when the average scene luminance falls lower than this.");
					ImGui::SliderFloat("Max Effect EV", &settings.Tonemapper.PurkinjeMaxEV, -6.f, 21.f, "%.2f EV");
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::Text("From this point onward, the Purkinje effect remains the greatest.");

					ImGui::TreePop();
				}

				ImGui::TreePop();
			}

			if (ImGui::TreeNodeEx("AgX", ImGuiTreeNodeFlags_DefaultOpen)) {
				ImGui::SliderFloat("Slope", &settings.Tonemapper.Slope, 0.f, 2.f, "%.2f");
				ImGui::SliderFloat("Power", &settings.Tonemapper.Power, 0.f, 2.f, "%.2f");
				ImGui::SliderFloat("Offset", &settings.Tonemapper.Offset, -1.f, 1.f, "%.2f");
				ImGui::SliderFloat("Saturation", &settings.Tonemapper.Saturation, 0.f, 2.f, "%.2f");
				ImGui::TreePop();
			}

			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Debug")) {
			ImGui::BulletText("texBloom");
			static int mip = 0;
			ImGui::SliderInt("Mip Level", &mip, 0, 8, "%d", ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp);
			ImGui::Image(texBloomMipSRVs[mip].get(), { texBloom->desc.Width * .2f, texBloom->desc.Height * .2f });

			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
}

void HDRBloom::Load(json& o_json)
{
	if (o_json[GetName()].is_object())
		settings = o_json[GetName()];

	Feature::Load(o_json);
}

void HDRBloom::Save(json& o_json) { o_json[GetName()] = settings; }

void HDRBloom::SetupResources()
{
	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	ID3D11Device* device = renderer->GetRuntimeData().forwarder;

	logger::debug("Creating buffers...");
	{
		autoExposureCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<AutoExposureCB>());
		bloomCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<BloomCB>());
		tonemapCB = std::make_unique<ConstantBuffer>(ConstantBufferDesc<TonemapCB>());
	}

	logger::debug("Creating 1D textures...");
	{
		D3D11_TEXTURE1D_DESC texDesc = {
			.Width = 257,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R32_UINT,
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_UNORDERED_ACCESS,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D,
			.Texture1D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE1D,
			.Texture1D = { .MipSlice = 0 }
		};

		texHistogram = std::make_unique<Texture1D>(texDesc);
		texHistogram->CreateUAV(uavDesc);

		texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R16_FLOAT;
		texDesc.Width = 1;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

		texAdaptation = std::make_unique<Texture1D>(texDesc);
		texAdaptation->CreateSRV(srvDesc);
		texAdaptation->CreateUAV(uavDesc);
	}

	logger::debug("Creating 2D textures...");
	{
		// texAdapt for adaptation
		auto gameTexMainCopy = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY];

		D3D11_TEXTURE2D_DESC texDesc;
		gameTexMainCopy.texture->GetDesc(&texDesc);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = (UINT)-1 }
		};

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

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
					.Texture2D = { .MostDetailedMip = i, .MipLevels = 1 }
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
	auto programPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\HDRBloom\\histogram.cs.hlsl", {}, "cs_5_0"));
	if (programPtr)
		histogramProgram.attach(programPtr);

	programPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\HDRBloom\\histogram.cs.hlsl", { { "AVG", "" } }, "cs_5_0"));
	if (programPtr)
		histogramAvgProgram.attach(programPtr);

	programPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\HDRBloom\\bloom.cs.hlsl", { { "DOWNSAMPLE", "" } }, "cs_5_0"));
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
	auto checkClear = [](winrt::com_ptr<ID3D11ComputeShader>& shader) {
		if (shader) {
			shader->Release();
			shader.detach();
		}
	};
	checkClear(histogramProgram);
	checkClear(histogramAvgProgram);
	checkClear(bloomDownsampleProgram);
	checkClear(bloomUpsampleProgram);
	checkClear(tonemapProgram);
	CompileComputeShaders();
}

void HDRBloom::Reset()
{
}

void HDRBloom::Draw(const RE::BSShader*, const uint32_t)
{
}

void HDRBloom::DrawPreProcess()
{
	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	auto context = renderer->GetRuntimeData().context;

	auto gameTexMain = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
	ResourceInfo lastTexColor = { gameTexMain.texture, gameTexMain.SRV };

	// Adaptation histogram
	if (settings.EnableAutoExposure && !settings.AdaptAfterBloom)
		DrawAdaptation(lastTexColor);

	// COD Bloom
	if (settings.EnableBloom)
		lastTexColor = DrawCODBloom(lastTexColor);

	if (settings.EnableAutoExposure && settings.AdaptAfterBloom)
		DrawAdaptation(lastTexColor);

	// AgX tonemap
	if (settings.EnableTonemapper)
		lastTexColor = DrawTonemapper(lastTexColor);

	// either MAIN_COPY or MAIN is used as input for HDR pass
	// so we copy to both so whatever the game wants we're not failing it
	context->CopySubresourceRegion(gameTexMain.texture, 0, 0, 0, 0, lastTexColor.tex, 0, nullptr);
	context->CopySubresourceRegion(
		renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN_COPY].texture,
		0, 0, 0, 0, lastTexColor.tex, 0, nullptr);
}

void HDRBloom::DrawAdaptation(ResourceInfo tex_input)
{
	auto context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;

	AutoExposureCB cbData = {
		.AdaptLerp = 1.f - exp(-RE::BSTimer::GetSingleton()->realTimeDelta * settings.AdaptSpeed),
		.AdaptArea = settings.AdaptArea,
		.MinLogLum = settings.HistogramRange.x - 3,  // log2(0.125)
		.LogLumRange = settings.HistogramRange.y - settings.HistogramRange.x
	};
	cbData.AdaptLerp = std::clamp(cbData.AdaptLerp, 0.f, 1.f);
	cbData.RcpLogLumRange = 1.f / cbData.LogLumRange;
	autoExposureCB->Update(cbData);

	// Calculate histogram
	ID3D11ShaderResourceView* srv = tex_input.srv;
	std::array<ID3D11UnorderedAccessView*, 2> uavs = { texHistogram->uav.get(), texAdaptation->uav.get() };
	ID3D11Buffer* cb = autoExposureCB->CB();
	context->CSSetConstantBuffers(0, 1, &cb);
	context->CSSetUnorderedAccessViews(0, (UINT)uavs.size(), uavs.data(), nullptr);
	context->CSSetShaderResources(0, 1, &srv);
	context->CSSetShader(histogramProgram.get(), nullptr, 0);

	context->Dispatch(((texTonemap->desc.Width - 1) >> 4) + 1, ((texTonemap->desc.Height - 1) >> 4) + 1, 1);

	// Calculate average
	context->CSSetShader(histogramAvgProgram.get(), nullptr, 0);
	context->Dispatch(1, 1, 1);

	// clean up
	srv = nullptr;
	uavs.fill(nullptr);
	cb = nullptr;
	context->CSSetUnorderedAccessViews(0, (UINT)uavs.size(), uavs.data(), nullptr);
	context->CSSetShaderResources(0, 1, &srv);
	context->CSSetConstantBuffers(0, 1, &cb);
	context->CSSetShader(nullptr, nullptr, 0);
}

HDRBloom::ResourceInfo HDRBloom::DrawCODBloom(HDRBloom::ResourceInfo input)
{
	auto context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;

	// update cb
	BloomCB cbData = {
		.IsZeroMip = true,
		.IsFirstMip = true,
		.UpsampleMult = 1.f,
		.CurrentMipMult = 1.f,
		.UpsampleRadius = settings.UpsampleRadius,
	};
	bloomCB->Update(cbData);

	// copy to lowest mip
	// TO BE CHANGED

	ID3D11ShaderResourceView* srv = input.srv;
	ID3D11UnorderedAccessView* uav = nullptr;
	ID3D11Buffer* cb = bloomCB->CB();
	context->CSSetConstantBuffers(0, 1, &cb);

	// downsample
	context->CSSetShader(bloomDownsampleProgram.get(), nullptr, 0);
	for (int i = 0; i < 9; i++) {
		if (i != 0) {
			if (i == 1) {
				cbData.IsZeroMip = false;
				cbData.IsFirstMip = true;
				bloomCB->Update(cbData);
			} else if (i == 2) {
				cbData.IsFirstMip = false;
				bloomCB->Update(cbData);
			}

			srv = texBloomMipSRVs[i - 1].get();
		}

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
		if (i == 0) {
			cbData.IsFirstMip = true;

			if (settings.EnableNormalisation) {
				float normalisationFactor = 0.f;
				for (int j = 7; j >= 0; j--)
					normalisationFactor += settings.MipBlendFactor[j];
				normalisationFactor = 1.f / (1 + normalisationFactor * settings.BlendFactor);
				cbData.NormalisationFactor = normalisationFactor;
			} else
				cbData.NormalisationFactor = 1.f;
		}

		cbData.UpsampleMult = 1.f;
		if (i == 7)
			cbData.UpsampleMult = settings.MipBlendFactor[i];
		else if (i == 0)
			cbData.UpsampleMult = settings.BlendFactor;
		cbData.CurrentMipMult = (i == 0) ? 1.f : settings.MipBlendFactor[i - 1];

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

HDRBloom::ResourceInfo HDRBloom::DrawTonemapper(HDRBloom::ResourceInfo tex_input)
{
	auto context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;

	// update cb
	TonemapCB cbData = { .settings = settings.Tonemapper };
	cbData.EnableAutoExposure = settings.EnableAutoExposure;
	cbData.settings.ExposureCompensation = exp2(cbData.settings.ExposureCompensation) * 0.125f;
	cbData.settings.AdaptationRange.x = exp2(cbData.settings.AdaptationRange.x) * 0.125f;
	cbData.settings.AdaptationRange.y = exp2(cbData.settings.AdaptationRange.y) * 0.125f;
	tonemapCB->Update(cbData);

	std::array<ID3D11ShaderResourceView*, 2> srvs = { tex_input.srv, texAdaptation->srv.get() };
	ID3D11UnorderedAccessView* uav = texTonemap->uav.get();
	ID3D11Buffer* cb = tonemapCB->CB();
	context->CSSetConstantBuffers(0, 1, &cb);
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	context->CSSetShaderResources(0, (UINT)srvs.size(), srvs.data());
	context->CSSetShader(tonemapProgram.get(), nullptr, 0);

	context->Dispatch(((texTonemap->desc.Width - 1) >> 5) + 1, ((texTonemap->desc.Height - 1) >> 5) + 1, 1);

	// clean up
	srvs.fill(nullptr);
	uav = nullptr;
	cb = nullptr;
	context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
	context->CSSetShaderResources(0, (UINT)srvs.size(), srvs.data());
	context->CSSetConstantBuffers(0, 1, &cb);
	context->CSSetShader(nullptr, nullptr, 0);

	return { texTonemap->resource.get(), texTonemap->srv.get() };
}