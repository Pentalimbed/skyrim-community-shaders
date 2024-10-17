#include "PhysicalWeather.h"

#include "State.h"
#include "Util.h"

inline float getDayInYear()
{
	auto calendar = RE::Calendar::GetSingleton();
	if (calendar)
		return (calendar->GetMonth() - 1) * 30 + (calendar->GetDay() - 1) + calendar->GetHour() / 24.f;
	return 0;
}

void PhysicalWeather::DrawSettings()
{
	auto calendar = RE::Calendar::GetSingleton();
	auto sky = RE::Sky::GetSingleton();
	auto climate = sky->currentClimate;

	RE::NiPoint3 cam_pos = { 0, 0, 0 };
	if (auto cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot)
		cam_pos = cam->cameraRoot->world.translate;

	// ImGui::InputFloat("Timer", &phys_sky_sb_data.timer, 0, 0, "%.6f", ImGuiInputTextFlags_ReadOnly);

	if (calendar) {
		ImGui::SeparatorText("Calendar");

		auto game_time = calendar->GetCurrentGameTime();
		auto game_hour = calendar->GetHour();
		auto game_day = calendar->GetDay();
		auto game_month = calendar->GetMonth();
		auto day_in_year = getDayInYear();
		ImGui::Text("Game Time: %.3f", game_time);
		ImGui::SameLine();
		ImGui::Text("Hour: %.3f", game_hour);
		ImGui::SameLine();
		ImGui::Text("Day: %.3f", game_day);
		ImGui::SameLine();
		ImGui::Text("Month: %u", game_month);
		ImGui::SameLine();
		ImGui::Text("Day in Year: %.3f", day_in_year);

		if (climate) {
			auto sunrise = climate->timing.sunrise;
			auto sunset = climate->timing.sunset;
			ImGui::Text("Sunrise: %u-%u", sunrise.GetBeginTime().tm_hour, sunrise.GetEndTime().tm_hour);
			ImGui::SameLine();
			ImGui::Text("Sunset: %u-%u", sunset.GetBeginTime().tm_hour, sunset.GetEndTime().tm_hour);
		}
	}

	ImGui::SeparatorText("Textures");
	{
		ImGui::BulletText("Transmittance LUT");
		ImGui::Image((void*)(transmittance_lut->srv.get()), { s_transmittance_width, s_transmittance_height });

		ImGui::BulletText("Multiscatter LUT");
		ImGui::Image((void*)(multiscatter_lut->srv.get()), { s_multiscatter_width, s_multiscatter_height });

		ImGui::BulletText("Sky-View LUT");
		ImGui::Image((void*)(sky_view_lut->srv.get()), { s_sky_view_width, s_sky_view_height });
	}
}

void PhysicalWeather::LoadSettings(json&)
{
}

void PhysicalWeather::SaveSettings(json&)
{
}

void PhysicalWeather::SetupResources()
{
	auto device = State::GetSingleton()->device;

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
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, linear_ccc_samp.put()));

		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_MIRROR;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, linear_wmc_samp.put()));
	}

	logger::debug("Creating constant buffers...");
	{
		phys_weather_cb = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<PhysWeatherCB>());
	}

	logger::debug("Creating LUT textures...");
	{
		D3D11_TEXTURE2D_DESC tex2d_desc{
			.Width = s_transmittance_width,
			.Height = s_transmittance_height,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
			.SampleDesc = { .Count = 1, .Quality = 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
		D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {
			.Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MostDetailedMip = 0, .MipLevels = 1 }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {
			.Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		transmittance_lut = eastl::make_unique<Texture2D>(tex2d_desc);
		transmittance_lut->CreateSRV(srv_desc);
		transmittance_lut->CreateUAV(uav_desc);

		tex2d_desc.Width = s_multiscatter_width;
		tex2d_desc.Height = s_multiscatter_height;

		multiscatter_lut = eastl::make_unique<Texture2D>(tex2d_desc);
		multiscatter_lut->CreateSRV(srv_desc);
		multiscatter_lut->CreateUAV(uav_desc);

		tex2d_desc.Width = s_sky_view_width;
		tex2d_desc.Height = s_sky_view_height;

		sky_view_lut = eastl::make_unique<Texture2D>(tex2d_desc);
		sky_view_lut->CreateSRV(srv_desc);
		sky_view_lut->CreateUAV(uav_desc);

		D3D11_TEXTURE3D_DESC tex3d_desc{
			.Width = s_aerial_perspective_width,
			.Height = s_aerial_perspective_height,
			.Depth = s_aerial_perspective_depth,
			.MipLevels = 1,
			.Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
		srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
		srv_desc.Texture3D = { .MostDetailedMip = 0, .MipLevels = 1 };
		uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D,
		uav_desc.Texture3D = { .MipSlice = 0, .FirstWSlice = 0, .WSize = s_aerial_perspective_depth };

		aerial_perspective_lut = eastl::make_unique<Texture3D>(tex3d_desc);
		aerial_perspective_lut->CreateSRV(srv_desc);
		aerial_perspective_lut->CreateUAV(uav_desc);
	}

	CompileComputeShaders();
}

void PhysicalWeather::ClearShaderCache()
{
	const auto shaderPtrs = std::array{
		&transmittance_cs, &multiscatter_cs, &sky_view_cs, &aerial_perspective_cs
	};

	for (auto shader : shaderPtrs)
		*shader = nullptr;

	CompileComputeShaders();
}

void PhysicalWeather::CompileComputeShaders()
{
	struct ShaderCompileInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* csPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines;
	};

	std::vector<ShaderCompileInfo> shaderInfos = {
		{ &transmittance_cs, "lut_gen.cs.hlsl", { { "LUTGEN", "0" } } },
		{ &multiscatter_cs, "lut_gen.cs.hlsl", { { "LUTGEN", "1" } } },
		{ &sky_view_cs, "lut_gen.cs.hlsl", { { "LUTGEN", "2" } } },
		{ &aerial_perspective_cs, "lut_gen.cs.hlsl", { { "LUTGEN", "3" } } }
	};

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\PhysicalWeather") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0")))
			info.csPtr->attach(rawPtr);
	}
}

void PhysicalWeather::Prepass()
{
	if ((RE::Sky::GetSingleton()->mode.get() == RE::Sky::Mode::kFull) &&
		RE::Sky::GetSingleton()->currentClimate) {
		UpdateCB();
		GenerateLuts();
	}
}

void PhysicalWeather::UpdateCB()
{
	cb_data = {
		.ground_albedo = settings.ground_albedo,
		.unit_scale = settings.unit_scale,
		.planet_radius = settings.planet_radius,
		.atmosphere_height = settings.atmosphere_height,
		.bottom_z = settings.bottom_z,
		.atmosphere = settings.atmosphere
	};

	auto dirLight = skyrim_cast<RE::NiDirectionalLight*>(RE::BSShaderManager::State::GetSingleton().shadowSceneNode[0]->GetRuntimeData().sunLight->light.get());
	const auto& direction = dirLight->GetWorldDirection();
	cb_data.dirlight_dir = { -direction.x, -direction.y, -direction.z };

	RE::NiPoint3 cam_pos = { 0, 0, 0 };
	if (auto cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot) {
		cam_pos = cam->cameraRoot->world.translate;
		cb_data.cam_height_km = (cam_pos.z - settings.bottom_z) * settings.unit_scale * 1.428e-5f + 6.36e3f;
	}
}

void PhysicalWeather::GenerateLuts()
{
	auto context = State::GetSingleton()->context;

	std::array<ID3D11ShaderResourceView*, 3> srvs = { nullptr };
	std::array<ID3D11UnorderedAccessView*, 1> uavs = { nullptr };
	std::array<ID3D11SamplerState*, 2> samplers = { linear_ccc_samp.get(), linear_wmc_samp.get() };
	auto cb = phys_weather_cb->CB();

	/* ---- DISPATCH ---- */
	context->CSSetConstantBuffers(0, 1, &cb);
	context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());

	// -> transmittance
	uavs.at(0) = transmittance_lut->uav.get();
	context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	context->CSSetShader(transmittance_cs.get(), nullptr, 0);
	context->Dispatch(((s_transmittance_width - 1) >> 5) + 1, ((s_transmittance_height - 1) >> 5) + 1, 1);

	// -> multiscatter
	uavs.at(0) = multiscatter_lut->uav.get();
	srvs.at(1) = transmittance_lut->srv.get();
	context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
	context->CSSetShader(multiscatter_cs.get(), nullptr, 0);
	context->Dispatch(((s_multiscatter_width - 1) >> 5) + 1, ((s_multiscatter_height - 1) >> 5) + 1, 1);

	// -> sky-view
	uavs.at(0) = sky_view_lut->uav.get();
	srvs.at(2) = multiscatter_lut->srv.get();
	context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
	context->CSSetShader(sky_view_cs.get(), nullptr, 0);
	context->Dispatch(((s_sky_view_width - 1) >> 5) + 1, ((s_sky_view_height - 1) >> 5) + 1, 1);

	// -> aerial perspective
	uavs.at(0) = aerial_perspective_lut->uav.get();
	context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	context->CSSetShader(aerial_perspective_cs.get(), nullptr, 0);
	context->Dispatch(((s_aerial_perspective_width - 1) >> 5) + 1, ((s_aerial_perspective_height - 1) >> 5) + 1, 1);

	/* ---- RESTORE ---- */
	srvs.fill(nullptr);
	uavs.fill(nullptr);
	samplers.fill(nullptr);
	cb = nullptr;

	context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetSamplers(0, (uint)samplers.size(), samplers.data());
	context->CSSetShader(nullptr, nullptr, 0);
}
