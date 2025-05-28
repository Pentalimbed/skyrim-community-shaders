#include "WeatherSim.h"

#include "Menu.h"
#include "State.h"

void WeatherSim::BasicDispatch(
	std::span<ID3D11ShaderResourceView*> srv2ds,
	std::span<ID3D11UnorderedAccessView*> uav2ds,
	std::span<ID3D11ShaderResourceView*> srv3ds,
	std::span<ID3D11UnorderedAccessView*> uav3ds,
	ID3D11ComputeShader* cs, bool is3D)
{
	auto context = globals::d3d::context;

	auto buffers = std::array{ simCb->CB(), outCb->CB(), jumpFloodCb->CB() };

	/* ---- DISPATCH ---- */
	context->CSSetConstantBuffers(0, (uint)buffers.size(), buffers.data());
	context->CSSetUnorderedAccessViews(0, (uint)uav2ds.size(), uav2ds.data(), nullptr);
	context->CSSetUnorderedAccessViews(k3DRWTexSlot, (uint)uav3ds.size(), uav3ds.data(), nullptr);
	context->CSSetShaderResources(0, (uint)srv2ds.size(), srv2ds.data());
	context->CSSetShaderResources(k3DTexSlot, (uint)srv3ds.size(), srv3ds.data());
	context->CSSetShader(cs, nullptr, 0);
	context->Dispatch((kCloudW + 7) >> 3, (kCloudH + 7) >> 3, is3D ? kCloudD : 1);

	/* ---- RESTORE ---- */
	buffers.fill(nullptr);
	std::ranges::fill(srv2ds, nullptr);
	std::ranges::fill(uav2ds, nullptr);
	std::ranges::fill(srv3ds, nullptr);
	std::ranges::fill(uav3ds, nullptr);

	context->CSSetConstantBuffers(0, (uint)buffers.size(), buffers.data());
	context->CSSetUnorderedAccessViews(0, (uint)uav2ds.size(), uav2ds.data(), nullptr);
	context->CSSetUnorderedAccessViews(k3DRWTexSlot, (uint)uav3ds.size(), uav3ds.data(), nullptr);
	context->CSSetShaderResources(0, (uint)srv2ds.size(), srv2ds.data());
	context->CSSetShaderResources(k3DTexSlot, (uint)srv3ds.size(), srv3ds.data());
	context->CSSetShader(cs, nullptr, 0);
}

WeatherSim::WeatherSim()
{
	tasks.push_back([&]() {
		auto srv3ds = std::array{ texTheta->srv.get() };
		auto uav3ds = std::array{ texQV->uav.get() };
		BasicDispatch({}, {}, srv3ds, uav3ds, csGroundEva.get(), false);
	});

	// source & vort confine
	// tasks.push_back([&]() {
	// 	auto srv3ds = std::array{ texU->srv.get() };
	// 	auto uav3ds = std::array{ tex3dVec3Temps[0]->uav.get() };
	// 	BasicDispatch({}, {}, srv3ds, uav3ds, csVorticity.get(), true);
	// });
	// tasks.push_back([&]() {
	// 	auto srv3ds = std::array{ tex3dVec3Temps[0]->srv.get() };
	// 	auto uav3ds = std::array{ texU->uav.get() };
	// 	BasicDispatch({}, {}, srv3ds, uav3ds, csVortConfine.get(), true);
	// });
	tasks.push_back([&]() {
		auto srv3ds = std::array{ texQV->srv.get(), texQC->srv.get(), texTheta->srv.get() };
		auto uav3ds = std::array{ texU->uav.get() };
		BasicDispatch({}, {}, srv3ds, uav3ds, csBuoyExtForce.get(), true);
	});

	// project
	tasks.push_back([&]() {
		auto srv3ds = std::array{ texU->srv.get() };
		auto uav3ds = std::array{ tex3dTemps[0]->uav.get(), tex3dTemps[1]->uav.get() };
		BasicDispatch({}, {}, srv3ds, uav3ds, csDivVel.get(), true);
	});
	for (int i = 0; i < 20; i++) {
		tasks.push_back([&]() {
			auto srv3ds = std::array{ tex3dTemps[0]->srv.get(), tex3dTemps[1]->srv.get() };
			auto uav3ds = std::array{ tex3dTemps[2]->uav.get() };
			BasicDispatch({}, {}, srv3ds, uav3ds, csProjectIter.get(), true);
			tex3dTemps[1].swap(tex3dTemps[2]);
		});
	}
	tasks.push_back([&]() {
		auto srv3ds = std::array{ tex3dTemps[1]->srv.get() };
		auto uav3ds = std::array{ texU->uav.get() };
		BasicDispatch({}, {}, srv3ds, uav3ds, csProject.get(), true);
	});

	// advect
	tasks.push_back([&]() {
		auto srv3ds = std::array{ texU->srv.get() };
		auto uav3ds = std::array{ tex3dVec3Temps[0]->uav.get() };
		BasicDispatch({}, {}, srv3ds, uav3ds, csAdvectVel.get(), true);
		tex3dVec3Temps[0].swap(texU);
	});

	// diffuse
	// tasks.push_back([&]() {
	// 	globals::d3d::context->CopyResource(tex3dVec3Temps[0]->resource.get(), texU->resource.get());
	// 	globals::d3d::context->CopyResource(tex3dVec3Temps[1]->resource.get(), texU->resource.get());
	// });
	// for (int i = 0; i < 20; i++) {
	// 	tasks.push_back([&]() {
	// 		auto srv3ds = std::array{ texU->srv.get(), tex3dVec3Temps[0]->srv.get() };
	// 		auto uav3ds = std::array{ tex3dVec3Temps[1]->uav.get() };
	// 		BasicDispatch({}, {}, srv3ds, uav3ds, csDiffVelIter.get(), true);

	// 		tex3dVec3Temps[0].swap(tex3dVec3Temps[1]);
	// 	});
	// }
	// tasks.push_back([&]() { tex3dVec3Temps[0].swap(texU); });

	// advect props
	tasks.push_back([&]() {
		auto srv3ds = std::array{ texU->srv.get(), texQV->srv.get(), texQC->srv.get(), texQP->srv.get(), texTheta->srv.get() };
		auto uav3ds = std::array{ tex3dTemps[0]->uav.get(), tex3dTemps[1]->uav.get(), tex3dTemps[2]->uav.get(), tex3dTemps[3]->uav.get() };
		BasicDispatch({}, {}, srv3ds, uav3ds, csAdvectProps.get(), true);

		tex3dTemps[0].swap(texQV);
		tex3dTemps[1].swap(texQC);
		tex3dTemps[2].swap(texQP);
		tex3dTemps[3].swap(texTheta);
	});

	// microphysics
	tasks.push_back([&]() {
		auto srv3ds = std::array{ texQV->srv.get(), texQC->srv.get(), texQP->srv.get(), texTheta->srv.get() };
		auto uav3ds = std::array{ tex3dTemps[0]->uav.get(), tex3dTemps[1]->uav.get(), tex3dTemps[2]->uav.get(), tex3dTemps[3]->uav.get() };
		BasicDispatch({}, {}, srv3ds, uav3ds, csWaterMicrophysics.get(), true);

		tex3dTemps[0].swap(texQV);
		tex3dTemps[1].swap(texQC);
		tex3dTemps[2].swap(texQP);
		tex3dTemps[3].swap(texTheta);
	});

	// output
	tasks.push_back([&]() {
		auto srvs = std::array{ texQC->srv.get() };
		auto uavs = std::array{ texCloudSites[0]->uav.get() };
		BasicDispatch(srvs, uavs, {}, {}, csMask.get(), true);
	});

	JumpFloodCB data{
		.stride = { kCloudW / 2, kCloudH / 2, kCloudD / 2 }
	};
	while (true) {
		tasks.push_back([data, this]() {
			jumpFloodCb->Update(data);
			auto srvs = std::array{ texCloudSites[0]->srv.get() };
			auto uavs = std::array{ texCloudSites[1]->uav.get() };
			BasicDispatch(srvs, uavs, {}, {}, csJumpFlood.get(), true);
			texCloudSites[0].swap(texCloudSites[1]);
		});

		data.stride[0] /= 2;
		data.stride[1] /= 2;
		data.stride[2] /= 2;
		if (data.stride[0] == 0 and data.stride[1] == 0 and data.stride[2] == 0)
			break;
		else {
			if (data.stride[0] < 1)
				data.stride[0] = 1;
			if (data.stride[1] < 1)
				data.stride[1] = 1;
			if (data.stride[2] < 1)
				data.stride[2] = 1;
		}
	}

	tasks.push_back([&]() {
		auto srvs = std::array{ texQC->srv.get(), texU->srv.get(), texCloudSites[0]->srv.get() };
		auto uavs = std::array{ texCloudMap->uav.get(), texCloudSdf->uav.get() };
		BasicDispatch(srvs, uavs, {}, {}, csAssemble.get(), true);
	});
}

void WeatherSim::DrawSettings()
{
	ImGui::SeparatorText("Simulation");

	ImGui::SliderFloat2("Global Wind", &settings.globalWind.x, -10.f, 10.f, "%.1f m/s");

	ImGui::SeparatorText("");

	if (ImGui::Button("Init States"))
		InitStates();

	if (ImGui::Button("Run cycle")) {
		while (!Update()) {}
	}

	if (ImGui::Button("Run step"))
		Update();
	ImGui::SameLine();
	ImGui::Text("Running step %d / %d", updProgress + 1, tasks.size());

	ImGui::SeparatorText("Visual");

	ImGui::SliderFloat2("Density Range", &settings.qcRange.x, 1e-2f, 1e-1f, "%.3f g/kg");
	ImGui::SliderFloat2("Updraft Range", &settings.uRange.x, -10.f, 10.f, "%.1f m/s");

	ImGui::SeparatorText("");

	ImGui::Checkbox("Per Frame Full Update", &enableDebugUpdate);
	ImGui::Checkbox("Debug Visualization", &enableDebugViz);
	ImGui::SliderInt("Debug Slice", &debugSlice, 0, kCloudD - 1);

	BUFFER_VIEWER_NODE_BULLET(texDebug, 1.f);
}

void WeatherSim::SetupResources()
{
	{
		simCb = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<SimCB>());
		outCb = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<OutCB>());
		jumpFloodCb = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<JumpFloodCB>());
	}

	// 2D
	{
		D3D11_TEXTURE2D_DESC texDesc{
			.Width = kCloudW,
			.Height = kCloudH,
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
			.Format = texDesc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
			.Format = texDesc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		texDebug = eastl::make_unique<Texture2D>(texDesc);
		texDebug->CreateSRV(srvDesc);
		texDebug->CreateUAV(uavDesc);
	}

	// 3D
	{
		D3D11_TEXTURE3D_DESC texDesc{
			.Width = kCloudW,
			.Height = kCloudH,
			.Depth = kCloudD,
			.MipLevels = 1,
			.Format = DXGI_FORMAT_R32_FLOAT,  // R16 results in precision err on theta
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

		for (auto tex : { &texQV, &texQC, &texQP, &texTheta }) {
			*tex = eastl::make_unique<Texture3D>(texDesc);
			(*tex)->CreateSRV(srvDesc);
			(*tex)->CreateUAV(uavDesc);
		}

		for (auto& tex : tex3dTemps) {
			tex = eastl::make_unique<Texture3D>(texDesc);
			tex->CreateSRV(srvDesc);
			tex->CreateUAV(uavDesc);
		}

		texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;

		for (auto tex : { &texU }) {
			*tex = eastl::make_unique<Texture3D>(texDesc);
			(*tex)->CreateSRV(srvDesc);
			(*tex)->CreateUAV(uavDesc);
		}

		for (auto& tex : tex3dVec3Temps) {
			tex = eastl::make_unique<Texture3D>(texDesc);
			tex->CreateSRV(srvDesc);
			tex->CreateUAV(uavDesc);
		}

		texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;

		texCloudMap = eastl::make_unique<Texture3D>(texDesc);
		texCloudMap->CreateSRV(srvDesc);
		texCloudMap->CreateUAV(uavDesc);

		texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R16_FLOAT;

		texCloudSdf = eastl::make_unique<Texture3D>(texDesc);
		texCloudSdf->CreateSRV(srvDesc);
		texCloudSdf->CreateUAV(uavDesc);

		texDesc.Format = srvDesc.Format = uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UINT;  // max 256x256x256

		for (auto& tex : texCloudSites) {
			tex = eastl::make_unique<Texture3D>(texDesc);
			tex->CreateSRV(srvDesc);
			tex->CreateUAV(uavDesc);
		}
	}

	CompileShaders();
}

void WeatherSim::CompileShaders()
{
	struct ShaderCompileInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* csPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines = {};
		std::string_view entry = "main";
	};

	constexpr auto ws = "WeatherSim.cs.hlsl";
	constexpr auto wso = "WeatherSimOut.cs.hlsl";

	std::vector<ShaderCompileInfo> shaderInfos = {
		{ &csInitStates, ws, {}, "initStates" },
		{ &csGroundEva, ws, {}, "groundEva" },
		{ &csAdvectVel, ws, {}, "advectVel" },
		{ &csDiffVelIter, ws, {}, "diffVelIter" },
		{ &csVorticity, ws, {}, "vorticity" },
		{ &csVortConfine, ws, {}, "vortConfine" },
		{ &csBuoyExtForce, ws, {}, "buoyExtForce" },
		{ &csDivVel, ws, {}, "divVel" },
		{ &csProjectIter, ws, {}, "projectIter" },
		{ &csProject, ws, {}, "project" },
		{ &csAdvectProps, ws, {}, "advectProps" },
		{ &csWaterMicrophysics, ws, {}, "waterMicrophysics" },
		{ &csDebugViz, ws, {}, "debugViz" },
		{ &csMask, wso, {}, "mask" },
		{ &csJumpFlood, wso, {}, "jumpFlood" },
		{ &csAssemble, wso, {}, "assemble" },
	};

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\PhysicalWeather") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0", info.entry.data())))
			info.csPtr->attach(rawPtr);
	}
}

bool WeatherSim::Update()
{
	tasks[updProgress]();

	updProgress++;
	if (updProgress >= static_cast<int>(tasks.size())) {
		updProgress = 0;
		return true;
	}
	return false;
}

void WeatherSim::InitStates()
{
	auto context = globals::d3d::context;

	{
		FLOAT clr[4] = { 0., 0., 0., 0. };
		context->ClearUnorderedAccessViewFloat(texQV->uav.get(), clr);
		context->ClearUnorderedAccessViewFloat(texQC->uav.get(), clr);
		context->ClearUnorderedAccessViewFloat(texQP->uav.get(), clr);
	}
	{
		auto uav3ds = std::array{ texU->uav.get(), texTheta->uav.get() };
		BasicDispatch({}, {}, {}, uav3ds, csInitStates.get(), true);
	}
}

void WeatherSim::PerFrame()
{
	{
		// TODO separate debugSlice update frequency with the rest
		SimCB data = {
			.dt = 10,
			.debugValues = float3((float)debugSlice, 0, 0),
			.tmpOcean = settings.tmpOcean + 273.15f,
			.epsConf = settings.epsConf,
			.globalWind = settings.globalWind,
		};
		simCb->Update(data);
	}
	{
		OutCB data = {
			.qcRange = settings.qcRange * 1e-3f,
			.uRange = settings.uRange
		};
		outCb->Update(data);
	}

	// TODO REMOVE DEBUG
	if (enableDebugUpdate)
		while (!Update()) {}

	if (enableDebugViz) {
		globals::state->BeginPerfEvent("DEBUG VIZ");

		auto srv3ds = std::array{ texU->srv.get(), texQV->srv.get(), texQC->srv.get(), texQP->srv.get(), texTheta->srv.get() };
		auto uav2ds = std::array{ texDebug->uav.get() };
		BasicDispatch({}, uav2ds, srv3ds, {}, csDebugViz.get(), false);

		globals::state->EndPerfEvent();
	}
}