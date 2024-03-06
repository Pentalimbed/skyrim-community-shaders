#include "HDRBloom.h"

#include "Util.h"

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	HDRBloom::GhostParameters,
	Mip,
	Scale,
	Intensity,
	Chromatic);

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	HDRBloom::Settings,
	EnableBloom,
	EnableGhosts,
	BloomThreshold,
	BloomUpsampleRadius,
	BloomBlendFactor,
	GhostsThreshold,
	GhostsCentralSize,
	GhostParams,
	NaturalVignetteFocal,
	NaturalVignettePower,
	MipBloomBlendFactor,
	EnableAutoExposure,
	AdaptAfterBloom,
	EnableTonemapper,
	HistogramRange,
	AdaptArea,
	AdaptSpeed,
	AdaptationRange,
	ExposureCompensation,
	Slope,
	Power,
	Offset,
	Saturation,
	PurkinjeStartEV,
	PurkinjeMaxEV,
	PurkinjeStrength,
	DitherMode)

void HDRBloom::DrawSettings()
{
	if (ImGui::BeginTabBar("##HDRBLOOM")) {
		if (ImGui::BeginTabItem("Bloom & Lens")) {
			ImGui::Checkbox("Enable Bloom", &settings.EnableBloom);
			ImGui::Checkbox("Enable Ghosts", &settings.EnableGhosts);

			ImGui::SeparatorText("Bloom");
			ImGui::PushID("Bloom");
			{
				ImGui::SliderFloat("Threshold", &settings.BloomThreshold, -6.f, 21.f, "%+.2f EV");
				ImGui::SliderFloat("Upsampling Radius", &settings.BloomUpsampleRadius, 1.f, 5.f, "%.1f px");
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text("A greater radius makes the bloom slightly blurrier.");

				ImGui::SliderFloat("Mix", &settings.BloomBlendFactor, 0.f, 1.f, "%.2f");

				ImGui::Separator();

				static int mipLevel = 1;
				ImGui::SliderInt("Mip Level", &mipLevel, 1, (int)settings.MipBloomBlendFactor.size() + 1, "%d", ImGuiSliderFlags_AlwaysClamp);
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text("The greater the level, the blurrier the part it controls");
				ImGui::Indent();
				{
					ImGui::SliderFloat("Intensity", &settings.MipBloomBlendFactor[mipLevel - 1], 0.f, 1.f, "%.2f");
				}
				ImGui::Unindent();
			}
			ImGui::PopID();

			ImGui::SeparatorText("Ghosts");
			ImGui::PushID("Ghosts");
			{
				ImGui::SliderFloat("Threshold", &settings.GhostsThreshold, -6.f, 21.f, "%+.2f EV");
				ImGui::SliderFloat("Central Size", &settings.GhostsCentralSize, 0.1f, 1.f, "%.2f");
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text("The size of the central area where lights may cause ghosting.");

				ImGui::Separator();

				static int item = 0;
				ImGui::SliderInt("Individual Control", &item, 0, (int)settings.GhostParams.size(), "%d", ImGuiSliderFlags_AlwaysClamp);
				ImGui::Indent();
				{
					auto& ghostParams = settings.GhostParams[item];
					ImGui::SliderInt("Source Mip Level", (int*)&ghostParams.Mip, 1, (int)s_BloomMips, "%d", ImGuiSliderFlags_AlwaysClamp);
					ImGui::SliderFloat("Scale", &ghostParams.Scale, -3.f, 3.f, "%.2f");
					ImGui::SliderFloat("Intensity", &ghostParams.Intensity, -3.f, 3.f, "%.2f");
					ImGui::SliderFloat("Chromatic Aberration", &ghostParams.Chromatic, -.1f, .1f, "%.3f");
				}
				ImGui::Unindent();
			}
			ImGui::PopID();

			ImGui::SeparatorText("Vignette");
			{
				ImGui::TextWrapped("Set Natural Vignette Power to 0 to disable.");
				ImGui::SliderFloat("Natural Vignette Focal Length", &settings.NaturalVignetteFocal, 0.1f, 2.f, "%.2f");
				ImGui::SliderFloat("Natural Vignette Power", &settings.NaturalVignettePower, 0.f, 4.f, "%.2f");
			}

			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Tonemapper")) {
			ImGui::Checkbox("Enable Tonemapper", &settings.EnableTonemapper);

			ImGui::SliderFloat("Exposure Compensation", &settings.ExposureCompensation, -6.f, 21.f, "%+.2f EV");
			if (auto _tt = Util::HoverTooltipWrapper())
				ImGui::Text("Adding/subtracting additional exposure to the image.");

			ImGui::SeparatorText("Auto Exposure");
			{
				ImGui::Checkbox("Enable Auto Exposure", &settings.EnableAutoExposure);
				ImGui::Checkbox("Adapt After Bloom", &settings.AdaptAfterBloom);

				ImGui::SliderFloat("Adaptation Speed", &settings.AdaptSpeed, 0.1f, 5.f, "%.2f");
				ImGui::SliderFloat2("Focus Area", &settings.AdaptArea.x, 0.f, 1.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text("[Width, Height] Specifies the proportion of the area that auto exposure will adapt to.");
				ImGui::SliderFloat2("Adaptation Range", &settings.AdaptationRange.x, -6.f, 21.f, "%.2f EV");
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text(
						"[Min, Max] The average scene luminance will be clamped between them when doing auto exposure."
						"Turning up the minimum, for example, makes it adapt less to darkness and therefore prevents over-brightening of dark scenes.");
				ImGui::SliderFloat2("Histogram Range", &settings.HistogramRange.x, -6.f, 21.f, "%.2f EV");
				if (auto _tt = Util::HoverTooltipWrapper())
					ImGui::Text(
						"[Min, Max] The range of luminance the algorithm recognises. "
						"Should be set to encompass the whole range of intensities the game could possibly produce for best accuracy. "
						"Usually requires no change.");

				if (ImGui::TreeNodeEx("Purkinje Effect", ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::TextWrapped("The Purkinje effect simulates the blue shift of human vision under low light.");

					ImGui::SliderFloat("Max Strength", &settings.PurkinjeStrength, 0.1f, 5.f, "%.2f");
					ImGui::SliderFloat("Fade In EV", &settings.PurkinjeStartEV, -6.f, 21.f, "%.2f EV");
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::Text("The Purkinje effect will start to take place when the average scene luminance falls lower than this.");
					ImGui::SliderFloat("Max Effect EV", &settings.PurkinjeMaxEV, -6.f, 21.f, "%.2f EV");
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::Text("From this point onward, the Purkinje effect remains the greatest.");

					ImGui::TreePop();
				}
			}

			ImGui::SeparatorText("AgX (ASC CDL)");
			{
				ImGui::SliderFloat("Slope", &settings.Slope, 0.f, 2.f, "%.2f");
				ImGui::SliderFloat("Power", &settings.Power, 0.f, 2.f, "%.2f");
				ImGui::SliderFloat("Offset", &settings.Offset, -1.f, 1.f, "%.2f");
				ImGui::SliderFloat("Saturation", &settings.Saturation, 0.f, 2.f, "%.2f");
			}

			ImGui::SeparatorText("Dither");
			{
				if (ImGui::BeginTable("Dither Mode Table", 3)) {
					ImGui::TableNextColumn();
					ImGui::RadioButton("Disabled", &settings.DitherMode, 0);
					ImGui::TableNextColumn();
					ImGui::RadioButton("Static", &settings.DitherMode, 1);
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::Text("RGB shift by CeeJayDK");
					ImGui::TableNextColumn();
					ImGui::RadioButton("Temporal", &settings.DitherMode, 2);
					if (auto _tt = Util::HoverTooltipWrapper())
						ImGui::Text("Triangular dither by The Sandvich Maker & TreyM");

					ImGui::EndTable();
				}
			}

			ImGui::EndTabItem();
		}

		if (ImGui::BeginTabItem("Debug")) {
			static int mip = 0;
			ImGui::SliderInt("Mip Level", &mip, 0, (int)s_BloomMips - 1, "%d", ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp);

			ImGui::BulletText("texBloom");
			ImGui::Image(texBloomMipSRVs[mip].get(), { texBloom->desc.Width * .2f, texBloom->desc.Height * .2f });

			ImGui::BulletText("texGhostsBlur");
			ImGui::Image(texGhostsMipSRVs[mip].get(), { texGhostsBlur->desc.Width * .2f, texGhostsBlur->desc.Height * .2f });

			ImGui::BulletText("texGhosts");
			ImGui::Image(texGhosts->srv.get(), { texGhosts->desc.Width * .4f, texGhosts->desc.Height * .4f });

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

		ghostsSB = std::make_unique<StructuredBuffer>(StructuredBufferDesc<GhostParameters>(7u), 7);
		ghostsSB->CreateSRV();
	}
	logger::debug("Creating 1D textures...");
	{
		D3D11_TEXTURE1D_DESC texDesc = {
			.Width = 256,
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
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};

		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		// texTonemap
		{
			texDesc.MipLevels = srvDesc.Texture2D.MipLevels = 1;
			texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
			texDesc.MiscFlags = 0;

			texTonemap = std::make_unique<Texture2D>(texDesc);
			texTonemap->CreateSRV(srvDesc);
			texTonemap->CreateUAV(uavDesc);
		}

		// texBloom/texGhostsBlur
		{
			texDesc.MipLevels = srvDesc.Texture2D.MipLevels = s_BloomMips;

			texBloom = std::make_unique<Texture2D>(texDesc);
			texBloom->CreateSRV(srvDesc);
			texGhostsBlur = std::make_unique<Texture2D>(texDesc);
			texGhostsBlur->CreateSRV(srvDesc);

			// SRV for each mip
			for (uint i = 0; i < s_BloomMips; i++) {
				D3D11_SHADER_RESOURCE_VIEW_DESC mipSrvDesc = {
					.Format = texDesc.Format,
					.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
					.Texture2D = { .MostDetailedMip = i, .MipLevels = 1 }
				};
				DX::ThrowIfFailed(device->CreateShaderResourceView(texBloom->resource.get(), &mipSrvDesc, texBloomMipSRVs[i].put()));
				DX::ThrowIfFailed(device->CreateShaderResourceView(texGhostsBlur->resource.get(), &mipSrvDesc, texGhostsMipSRVs[i].put()));
			}

			// UAV for each mip
			for (uint i = 0; i < s_BloomMips; i++) {
				D3D11_UNORDERED_ACCESS_VIEW_DESC mipUavDesc = {
					.Format = texDesc.Format,
					.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
					.Texture2D = { .MipSlice = i }
				};
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texBloom->resource.get(), &mipUavDesc, texBloomMipUAVs[i].put()));
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texGhostsBlur->resource.get(), &mipUavDesc, texGhostsMipUAVs[i].put()));
			}
		}

		// texGhosts
		{
			texDesc.MipLevels = srvDesc.Texture2D.MipLevels = 1;
			texDesc.Width >>= 1;
			texDesc.Height >>= 1;

			texGhosts = std::make_unique<Texture2D>(texDesc);
			texGhosts->CreateSRV(srvDesc);
			texGhosts->CreateUAV(uavDesc);
		}
	}

	logger::debug("Creating samplers...");
	{
		D3D11_SAMPLER_DESC samplerDesc = {
			.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
			.AddressU = D3D11_TEXTURE_ADDRESS_BORDER,
			.AddressV = D3D11_TEXTURE_ADDRESS_BORDER,
			.AddressW = D3D11_TEXTURE_ADDRESS_BORDER,
			.MaxAnisotropy = 1,
			.MinLOD = 0,
			.MaxLOD = D3D11_FLOAT32_MAX
		};

		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, colorSampler.put()));
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

	programPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\HDRBloom\\bloom.cs.hlsl", {}, "cs_5_0", "CS_Threshold"));
	if (programPtr)
		bloomThresholdProgram.attach(programPtr);

	for (uint i = 0; i < 3; ++i) {
		std::vector<std::pair<const char*, const char*>> defines = {};
		if (i != 1)
			defines.push_back({ "BLOOM", "" });
		if (i != 0)
			defines.push_back({ "GHOSTS", "" });

		programPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\HDRBloom\\bloom.cs.hlsl", defines, "cs_5_0", "CS_Downsample"));
		if (programPtr)
			bloomDownsampleProgram[i].attach(programPtr);
	}

	programPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\HDRBloom\\bloom.cs.hlsl", {}, "cs_5_0", "CS_Upsample"));
	if (programPtr)
		bloomUpsampleProgram.attach(programPtr);

	programPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\HDRBloom\\bloom.cs.hlsl", {}, "cs_5_0", "CS_Ghosts"));
	if (programPtr)
		bloomGhostsProgram.attach(programPtr);

	programPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\HDRBloom\\bloom.cs.hlsl", {}, "cs_5_0", "CS_Composite"));
	if (programPtr)
		bloomCompositeProgram.attach(programPtr);

	// programPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\HDRBloom\\bloom.cs.hlsl", {}, "cs_5_0", "CS_Blur"));
	// if (programPtr)
	// 	bloomBlurProgram.attach(programPtr);

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
	checkClear(bloomThresholdProgram);
	for (uint i = 0; i < 3; ++i)
		checkClear(bloomDownsampleProgram[i]);
	checkClear(bloomUpsampleProgram);
	checkClear(bloomGhostsProgram);
	checkClear(bloomCompositeProgram);
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
	if (settings.EnableBloom || settings.EnableGhosts)
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
		.Thresholds = float2{ exp2(settings.BloomThreshold), exp2(settings.GhostsThreshold) } * .125f,
		.IsFirstMip = true,
		.UpsampleRadius = settings.BloomUpsampleRadius,
		.UpsampleMult = 1.f,
		.CurrentMipMult = 1.f,
		.GhostsCentralSize = settings.GhostsCentralSize,
		.NaturalVignetteFocal = settings.NaturalVignetteFocal,
		.NaturalVignettePower = settings.NaturalVignettePower,
	};
	// cbData.BlurSigmaFactor.x = 2.f * RE::NI_PI * settings.GhostsBlurSigma * settings.GhostsBlurSigma;
	// cbData.BlurSigmaFactor.y = 1.f / cbData.BlurSigmaFactor.x;
	bloomCB->Update(cbData);

	// update sb
	ghostsSB->Update(settings.GhostParams.data(), sizeof(GhostParameters) * settings.GhostParams.size());

	struct ShaderState
	{
		ID3D11ShaderResourceView* srvs[4] = { nullptr };
		ID3D11ComputeShader* shader = nullptr;
		ID3D11Buffer* buffer = nullptr;
		ID3D11UnorderedAccessView* uavs[2] = { nullptr };
		ID3D11SamplerState* sampler = nullptr;
	} nullstate, newstate;

	auto setState = [&](ShaderState& state) {
		context->CSSetShader(state.shader, nullptr, 0);
		context->CSSetShaderResources(0, ARRAYSIZE(state.srvs), state.srvs);
		context->CSSetConstantBuffers(0, 1, &state.buffer);
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(state.uavs), state.uavs, nullptr);
		context->CSSetSamplers(0, 1, &state.sampler);
	};
	setState(nullstate);

	newstate.sampler = colorSampler.get();
	newstate.buffer = bloomCB->CB();
	newstate.srvs[3] = ghostsSB->SRV();

	// Threshold
	{
		newstate.shader = bloomThresholdProgram.get();
		newstate.srvs[0] = input.srv;
		newstate.uavs[0] = texBloomMipUAVs[0].get();
		newstate.uavs[1] = texGhostsMipUAVs[0].get();
		setState(newstate);

		context->Dispatch(((texBloom->desc.Width - 1) >> 5) + 1, ((texBloom->desc.Height - 1) >> 5) + 1, 1);
	}

	setState(nullstate);

	// Downsample
	size_t shaderIdx = settings.EnableBloom + settings.EnableGhosts * 2 - 1;
	newstate.shader = bloomDownsampleProgram[shaderIdx].get();
	for (int i = 0; i < s_BloomMips - 1; i++) {
		if (i == 1) {
			cbData.IsFirstMip = true;
			bloomCB->Update(cbData);
		} else if (i == 2) {
			cbData.IsFirstMip = false;
			bloomCB->Update(cbData);
		}

		newstate.srvs[1] = texBloomMipSRVs[i].get();
		newstate.srvs[2] = texGhostsMipSRVs[i].get();
		newstate.uavs[0] = texBloomMipUAVs[i + 1].get();
		newstate.uavs[1] = texGhostsMipUAVs[i + 1].get();

		setState(newstate);

		uint mipWidth = texBloom->desc.Width >> (i + 1);
		uint mipHeight = texBloom->desc.Height >> (i + 1);
		context->Dispatch(((mipWidth - 1) >> 5) + 1, ((mipHeight - 1) >> 5) + 1, 1);

		setState(nullstate);
	}

	// ghosts
	{
		newstate.shader = bloomGhostsProgram.get();
		newstate.srvs[2] = texGhostsBlur->srv.get();
		newstate.uavs[1] = texGhosts->uav.get();

		setState(newstate);

		context->Dispatch(((texGhosts->desc.Width - 1) >> 5) + 1, ((texGhosts->desc.Height - 1) >> 5) + 1, 1);
	}

	setState(nullstate);

	// ghosts blur
	// context->GenerateMips(texGhosts->srv.get());
	// {
	// 	newstate.shader = bloomBlurProgram.get();
	// 	newstate.srvs[2] = texGhosts->srv.get();
	// 	newstate.uavs[1] = texGhostsMipUAVs[1].get();

	// 	setState(newstate);

	// 	context->Dispatch(((texGhosts->desc.Width - 1) >> 5) + 1, ((texGhosts->desc.Height - 1) >> 5) + 1, 1);
	// }

	// upsample
	newstate.shader = bloomUpsampleProgram.get();
	for (int i = s_BloomMips - 2; i >= 1; i--) {
		cbData.UpsampleMult = 1.f;
		if (i == s_BloomMips - 2)
			cbData.UpsampleMult = settings.MipBloomBlendFactor[i];
		cbData.CurrentMipMult = settings.MipBloomBlendFactor[i - 1];
		bloomCB->Update(cbData);

		newstate.srvs[1] = texBloomMipSRVs[i + 1].get();
		newstate.uavs[0] = texBloomMipUAVs[i].get();

		setState(newstate);

		uint mipWidth = texBloom->desc.Width >> i;
		uint mipHeight = texBloom->desc.Height >> i;
		context->Dispatch(((mipWidth - 1) >> 5) + 1, ((mipHeight - 1) >> 5) + 1, 1);

		setState(nullstate);
	}

	// composite
	{
		cbData.UpsampleMult = settings.BloomBlendFactor;
		bloomCB->Update(cbData);

		newstate.shader = bloomCompositeProgram.get();
		newstate.srvs[1] = settings.EnableBloom ? texBloomMipSRVs[1].get() : nullptr;
		newstate.srvs[2] = settings.EnableGhosts ? texGhosts->srv.get() : nullptr;
		newstate.uavs[0] = texBloomMipUAVs[0].get();
		newstate.uavs[1] = nullptr;

		setState(newstate);

		context->Dispatch(((texBloom->desc.Width - 1) >> 5) + 1, ((texBloom->desc.Height - 1) >> 5) + 1, 1);
	}

	setState(nullstate);

	return { texBloom->resource.get(), texBloomMipSRVs[0].get() };
}

HDRBloom::ResourceInfo HDRBloom::DrawTonemapper(HDRBloom::ResourceInfo tex_input)
{
	auto context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;

	static uint timer = 0;
	if (!RE::UI::GetSingleton()->GameIsPaused())
		timer += (size_t)(RE::GetSecondsSinceLastFrame() * 1000);

	// update cb
	TonemapCB cbData = {
		.AdaptationRange = { exp2(settings.AdaptationRange.x) * 0.125f, exp2(settings.AdaptationRange.y) * 0.125f },
		.ExposureCompensation = exp2(settings.ExposureCompensation),
		.Slope = settings.Slope,
		.Power = settings.Power,
		.Offset = settings.Offset,
		.Saturation = settings.Saturation,
		.PurkinjeStartEV = settings.PurkinjeStartEV,
		.PurkinjeMaxEV = settings.PurkinjeMaxEV,
		.PurkinjeStrength = settings.PurkinjeStrength,
		.EnableAutoExposure = settings.EnableAutoExposure,
		.DitherMode = (UINT)settings.DitherMode,
		.Timer = timer / 1000.f
	};
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