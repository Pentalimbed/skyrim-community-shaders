#include "PhysicalWeather.h"

#include "State.h"

#include <DDSTextureLoader.h>

void PhysicalWeather::LoadSettings(json&) {}
void PhysicalWeather::SaveSettings(json&) {}
void PhysicalWeather::RestoreDefaultSettings() { settings = {}; }

void PhysicalWeather::SetupResources()
{
	auto device = globals::d3d::device;
	auto context = globals::d3d::context;

	logger::debug("Creating samplers...");
	{
		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, sampTr.put()));

		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, sampSv.put()));

		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, sampNoise.put()));
	}

	logger::debug("Creating textures...");
	{
		D3D11_TEXTURE2D_DESC tex2dDesc{
			.Width = kTrLutW,
			.Height = kTrLutH,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
			.SampleDesc = { .Count = 1, .Quality = 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = tex2dDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = tex2dDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		texTrLut = eastl::make_unique<Texture2D>(tex2dDesc);
		texTrLut->CreateSRV(srvDesc);
		texTrLut->CreateUAV(uavDesc);

		tex2dDesc.Width = kMsLutW;
		tex2dDesc.Height = kMsLutH;

		texMsLut = eastl::make_unique<Texture2D>(tex2dDesc);
		texMsLut->CreateSRV(srvDesc);
		texMsLut->CreateUAV(uavDesc);

		tex2dDesc.Width = kSvLutW;
		tex2dDesc.Height = kSvLutH;

		texSvLut = eastl::make_unique<Texture2D>(tex2dDesc);
		texSvLut->CreateSRV(srvDesc);
		texSvLut->CreateUAV(uavDesc);

		D3D11_TEXTURE3D_DESC tex3dDesc{
			.Width = kApLutW,
			.Height = kApLutH,
			.Depth = kApLutD,
			.MipLevels = 1,
			.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
		srvDesc.Texture3D = { .MostDetailedMip = 0, .MipLevels = 1 };
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D,
		uavDesc.Texture3D = { .MipSlice = 0, .FirstWSlice = 0, .WSize = kApLutD };

		texApLut = eastl::make_unique<Texture3D>(tex3dDesc);
		texApLut->CreateSRV(srvDesc);
		texApLut->CreateUAV(uavDesc);
	}

	{
		D3D11_TEXTURE3D_DESC texDesc{
			.Width = kCloudW,
			.Height = kCloudH,
			.Depth = kCloudD,
			.MipLevels = 1,
			.Format = DXGI_FORMAT_R11G11B10_FLOAT,
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D,
			.Texture3D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D,
			.Texture3D = { .MipSlice = 0, .FirstWSlice = 0, .WSize = kCloudD }
		};

		texCloudProfile = eastl::make_unique<Texture3D>(texDesc);
		texCloudProfile->CreateSRV(srvDesc);
		texCloudProfile->CreateUAV(uavDesc);

		texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R16_FLOAT;

		texCloudSdf = eastl::make_unique<Texture3D>(texDesc);
		texCloudSdf->CreateSRV(srvDesc);
		texCloudSdf->CreateUAV(uavDesc);

		texCloudShadow = eastl::make_unique<Texture3D>(texDesc);
		texCloudShadow->CreateSRV(srvDesc);
		texCloudShadow->CreateUAV(uavDesc);
	}

	{
		D3D11_TEXTURE2D_DESC texDesc;
		auto mainTex = globals::game::renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		mainTex.texture->GetDesc(&texDesc);
		texDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
		texDesc.MipLevels = 1;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = texDesc.MipLevels }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		texMainViewTr = eastl::make_unique<Texture2D>(texDesc);
		texMainViewTr->CreateSRV(srvDesc);
		texMainViewTr->CreateUAV(uavDesc);

		texMainViewLum = eastl::make_unique<Texture2D>(texDesc);
		texMainViewLum->CreateSRV(srvDesc);
		texMainViewLum->CreateUAV(uavDesc);
	}

	logger::debug("Loading textures from files...");
	{
		DirectX::CreateDDSTextureFromFile(device, context, L"Data\\Shaders\\PhysicalWeather\\noise.dds", nullptr, srvCloudNoise.put());
	}

	CompileShaders();
	weatherSim.SetupResources();
}

void PhysicalWeather::ClearShaderCache()
{
	CompileShaders();
}

void PhysicalWeather::CompileShaders()
{
	struct ShaderCompileInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* csPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines = {};
		std::string_view entry = "main";
	};

	std::vector<ShaderCompileInfo> shaderInfos = {
		{ &csTrLutGen, "LutGen.cs.hlsl", { { "LUTGEN", "0" } } },
		{ &csMsLutGen, "LutGen.cs.hlsl", { { "LUTGEN", "1" } } },
		{ &csSvLutGen, "LutGen.cs.hlsl", { { "LUTGEN", "2" } } },
		{ &csApLutGen, "LutGen.cs.hlsl", { { "LUTGEN", "3" } } },
		{ &csMainView, "VolumeRendering.cs.hlsl" },
		{ &csCloudShadow, "VolumeRendering.cs.hlsl", {}, "renderShadow" },
		{ &csTestBall, "TestBallTexGen.cs.hlsl" },
	};

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\PhysicalWeather") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0", info.entry.data())))
			info.csPtr->attach(rawPtr);
	}

	weatherSim.CompileShaders();
}

bool PhysicalWeather::ShadersOK()
{
	return csTrLutGen && csMsLutGen && csSvLutGen && csApLutGen && csCloudShadow && csMainView;
}

void PhysicalWeather::Reset()
{
	auto accumulator = RE::BSGraphics::BSShaderAccumulator::GetCurrentAccumulator();

	bool allGood = settings.enabled && ShadersOK();

	// check worldspace
	bool worldspace_enabled = false;
	WorldspaceInfo worldspaceInfo = {};
	if (globals::game::tes)
		if (auto worldspace = globals::game::tes->GetRuntimeData2().worldSpace; worldspace) {
			std::string worldspaceName = worldspace->GetFormEditorID();
			worldspace_enabled = settings.worldspaceWhitelist.contains(worldspaceName);
			if (worldspace_enabled)
				worldspaceInfo = settings.worldspaceWhitelist.at(worldspaceName);
		}
	allGood &= worldspace_enabled;

	// resolution
	float2 res = globals::state->screenSize;
	float2 dynres = Util::ConvertToDynamic(res);
	dynres = { floor(dynres.x), floor(dynres.y) };

	cbData = {
		.texDim = res,
		.rcpTexDim = float2(1.0f) / res,
		.frameDim = dynres,
		.rcpFrameDim = float2(1.0f) / dynres,
		.enabled = allGood,
		.zBottom = worldspaceInfo.zBottom,
		.centre = worldspaceInfo.centre,
		.groundAlbedo = settings.groundAlbedo,
		.rayleighFalloff = settings.rayleighFalloff * kGameUnit2Km,
		.rayleighScatter = settings.rayleighScatter * 1e-3 * kGameUnit2Km,
		.aerosolFalloff = settings.aerosolFalloff * kGameUnit2Km,
		.aerosolPhaseG = settings.aerosolPhaseG,
		.aerosolScatter = settings.aerosolScatter * 1e-3 * kGameUnit2Km,
		.aerosolAbsorption = settings.aerosolAbsorption * 1e-3 * kGameUnit2Km,
		.ozoneAltitude = settings.ozoneAltitude * kKm2GameUnit,
		.ozoneThickness = settings.ozoneThickness * kKm2GameUnit,
		.ozoneAbsorption = settings.ozoneAbsorption * 1e-3 * kGameUnit2Km,
		.cloudDensityScale = settings.cloudDensityScale,
		.cloudScatter = settings.cloudScatter * kGameUnit2Km,
		.cloudNoiseFreq = 1.f / settings.cloudNoiseScale * kGameUnit2Km,
		.cloudAbsorption = settings.cloudAbsorption * kGameUnit2Km,
	};

	if (!cbData.enabled)
		return;

	auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(accumulator->GetRuntimeData().activeShadowSceneNode->GetRuntimeData().sunLight->light.get());
	if (dirLight) {
		auto lightDir = -dirLight->GetWorldDirection();
		cbData.lightDir = { lightDir.x, lightDir.y, lightDir.z };
	}

	cbData.lightColor = settings.sunlightColor;

	cbData.rPlanet = 6.36e3f * kKm2GameUnit;
	cbData.rAtmosphere = cbData.rPlanet + 60.f * kKm2GameUnit;

	RE::NiPoint3 posCam = { 0, 0, 0 };
	if (auto cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot) {
		posCam = cam->cameraRoot->world.translate;
		cbData.zCameraPlanet = posCam.z - cbData.zBottom + cbData.rPlanet;
	}
}

void PhysicalWeather::Prepass()
{
	weatherSim.PerFrame();

	if (cbData.enabled) {
		GenerateLuts();
		TestBallTexGen();
		RenderCloudShadow();
		RenderMainView();
	} else {
		auto context = globals::d3d::context;
		{
			FLOAT clr[4] = { 1., 1., 1., 1. };
			context->ClearUnorderedAccessViewFloat(texMainViewTr->uav.get(), clr);
		}
		{
			FLOAT clr[4] = { 0., 0., 0., 0. };
			context->ClearUnorderedAccessViewFloat(texMainViewLum->uav.get(), clr);
		}
	}
}

void PhysicalWeather::GenerateLuts()
{
	auto state = globals::state;
	auto context = globals::d3d::context;

	constexpr auto debugStr = "Physical Weather: LUT Generation";
	state->BeginPerfEvent(debugStr);
	{
		TracyD3D11Zone(state->tracyCtx, debugStr);

		auto samplers = std::array{ sampTr.get(), sampSv.get(), sampNoise.get() };
		std::array<ID3D11ShaderResourceView*, 2> srvs = {};
		ID3D11UnorderedAccessView* uav = nullptr;

		/* ---- DISPATCH ---- */
		context->CSSetSamplers(0, (int)samplers.size(), samplers.data());

		// -> transmittance
		uav = texTrLut->uav.get();
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShader(csTrLutGen.get(), nullptr, 0);
		context->Dispatch((kTrLutW + 7) >> 3, (kTrLutH + 7) >> 3, 1);

		// -> multiscatter
		uav = texMsLut->uav.get();
		srvs.at(0) = texTrLut->srv.get();
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShaderResources(0, (int)srvs.size(), srvs.data());
		context->CSSetShader(csMsLutGen.get(), nullptr, 0);
		context->Dispatch((kMsLutW + 7) >> 3, (kMsLutH + 7) >> 3, 1);

		// -> sky-view
		uav = texSvLut->uav.get();
		srvs.at(1) = texMsLut->srv.get();
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShaderResources(0, (int)srvs.size(), srvs.data());
		context->CSSetShader(csSvLutGen.get(), nullptr, 0);
		context->Dispatch((kSvLutW + 7) >> 3, (kSvLutH + 7) >> 3, 1);

		// -> aerial perspective
		uav = texApLut->uav.get();
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShader(csApLutGen.get(), nullptr, 0);
		context->Dispatch((kApLutW + 7) >> 3, (kApLutH + 7) >> 3, 1);

		/* ---- RESTORE ---- */
		samplers.fill(nullptr);
		srvs.fill(nullptr);
		uav = nullptr;

		context->CSSetSamplers(0, (int)samplers.size(), samplers.data());
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShaderResources(0, (int)srvs.size(), srvs.data());
		context->CSSetShader(nullptr, nullptr, 0);
	}
	state->EndPerfEvent();
}

void PhysicalWeather::TestBallTexGen()
{
	auto context = globals::d3d::context;

	auto uavs = std::array{ texCloudProfile->uav.get(), texCloudSdf->uav.get() };

	/* ---- DISPATCH ---- */
	context->CSSetUnorderedAccessViews(0, (int)uavs.size(), uavs.data(), nullptr);
	context->CSSetShader(csTestBall.get(), nullptr, 0);
	context->Dispatch((kCloudW + 7) >> 3, (kCloudH + 7) >> 3, kCloudD);

	/* ---- RESTORE ---- */
	uavs.fill(nullptr);
	context->CSSetUnorderedAccessViews(0, (int)uavs.size(), uavs.data(), nullptr);
	context->CSSetShader(nullptr, nullptr, 0);
}

void PhysicalWeather::RenderCloudShadow()
{
	auto state = globals::state;
	auto context = globals::d3d::context;

	float3 rayPxDir = -cbData.lightDir;
	rayPxDir.x *= kCloudW / kCloudRange.x;
	rayPxDir.y *= kCloudH / kCloudRange.y;
	rayPxDir.z *= kCloudD / kCloudRange.z;
	float dir_max_component = std::max(std::max(abs(rayPxDir.x), abs(rayPxDir.y)), abs(rayPxDir.z));
	uint dispatchSize[2];
	if (abs(rayPxDir.x) == dir_max_component) {
		dispatchSize[0] = kCloudH;
		dispatchSize[1] = kCloudD;
	} else if (abs(rayPxDir.y) == dir_max_component) {
		dispatchSize[0] = kCloudW;
		dispatchSize[1] = kCloudD;
	} else {
		dispatchSize[0] = kCloudW;
		dispatchSize[1] = kCloudH;
	}

	constexpr auto debugStr = "Physical Weather: Cloud Shadow";
	state->BeginPerfEvent(debugStr);
	{
		TracyD3D11Zone(state->tracyCtx, debugStr);

		auto samplers = std::array{ sampTr.get(), sampSv.get(), sampNoise.get() };
		auto srvs = std::array{
			texTrLut->srv.get(),
			texMsLut->srv.get(),
			texSvLut->srv.get(),
			texApLut->srv.get(),
			globals::game::renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY].depthSRV,
			srvCloudNoise.get(),
			texCloudProfile->srv.get(),
			texCloudSdf->srv.get(),
		};
		auto uav = texCloudShadow->uav.get();

		/* ---- DISPATCH ---- */
		context->CSSetSamplers(0, (int)samplers.size(), samplers.data());
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShaderResources(0, (int)srvs.size(), srvs.data());
		context->CSSetShader(csCloudShadow.get(), nullptr, 0);
		context->Dispatch(dispatchSize[0], dispatchSize[1], 1);

		/* ---- RESTORE ---- */
		samplers.fill(nullptr);
		srvs.fill(nullptr);
		uav = nullptr;

		context->CSSetSamplers(0, (int)samplers.size(), samplers.data());
		context->CSSetUnorderedAccessViews(0, 1, &uav, nullptr);
		context->CSSetShaderResources(0, (int)srvs.size(), srvs.data());
		context->CSSetShader(nullptr, nullptr, 0);
	}
	state->EndPerfEvent();
}

void PhysicalWeather::RenderMainView()
{
	auto state = globals::state;
	auto context = globals::d3d::context;

	float2 size = Util::ConvertToDynamic(state->screenSize);
	uint resolution[2] = { (uint)size.x, (uint)size.y };

	constexpr auto debugStr = "Physical Weather: Main View";
	state->BeginPerfEvent(debugStr);
	{
		TracyD3D11Zone(state->tracyCtx, debugStr);

		auto samplers = std::array{ sampTr.get(), sampSv.get(), sampNoise.get() };
		auto srvs = std::array{
			texTrLut->srv.get(),
			texMsLut->srv.get(),
			texSvLut->srv.get(),
			texApLut->srv.get(),
			globals::game::renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY].depthSRV,
			srvCloudNoise.get(),
			texCloudProfile->srv.get(),
			texCloudSdf->srv.get(),
			texCloudShadow->srv.get(),
		};
		auto uavs = std::array{ texMainViewTr->uav.get(), texMainViewLum->uav.get() };

		/* ---- DISPATCH ---- */
		context->CSSetSamplers(0, (int)samplers.size(), samplers.data());
		context->CSSetUnorderedAccessViews(0, (int)uavs.size(), uavs.data(), nullptr);
		context->CSSetShaderResources(0, (int)srvs.size(), srvs.data());
		context->CSSetShader(csMainView.get(), nullptr, 0);
		context->Dispatch((resolution[0] + 7) >> 3, (resolution[1] + 7) >> 3, 1);

		/* ---- RESTORE ---- */
		samplers.fill(nullptr);
		srvs.fill(nullptr);
		uavs.fill(nullptr);

		context->CSSetSamplers(0, (int)samplers.size(), samplers.data());
		context->CSSetUnorderedAccessViews(0, (int)uavs.size(), uavs.data(), nullptr);
		context->CSSetShaderResources(0, (int)srvs.size(), srvs.data());
		context->CSSetShader(nullptr, nullptr, 0);
	}
	state->EndPerfEvent();
}