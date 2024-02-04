#include "../PhysicalSky.h"

#include "State.h"
#include "Util.h"

bool PhysicalSky::HasShaderDefine(RE::BSShader::Type type)
{
	switch (type) {
	case RE::BSShader::Type::Sky:
	case RE::BSShader::Type::Lighting:
	case RE::BSShader::Type::Grass:
	case RE::BSShader::Type::DistantTree:
		return true;
		break;
	default:
		return false;
		break;
	}
}

void PhysicalSky::SetupResources()
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
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, transmittance_sampler.put()));

		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_MIRROR;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, sky_view_sampler.put()));
	}

	logger::debug("Creating structured buffers...");
	{
		phys_sky_sb = std::make_unique<StructuredBuffer>(StructuredBufferDesc<PhysSkySB>(), 1);
		phys_sky_sb->CreateSRV();

		sky_per_geo_sb = std::make_unique<StructuredBuffer>(StructuredBufferDesc<SkyPerGeometrySB>(), 1);
		sky_per_geo_sb->CreateSRV();
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

		transmittance_lut = std::make_unique<Texture2D>(tex2d_desc);
		transmittance_lut->CreateSRV(srv_desc);
		transmittance_lut->CreateUAV(uav_desc);

		tex2d_desc.Width = s_multiscatter_width;
		tex2d_desc.Height = s_multiscatter_height;

		multiscatter_lut = std::make_unique<Texture2D>(tex2d_desc);
		multiscatter_lut->CreateSRV(srv_desc);
		multiscatter_lut->CreateUAV(uav_desc);

		tex2d_desc.Width = s_sky_view_width;
		tex2d_desc.Height = s_sky_view_height;

		sky_view_lut = std::make_unique<Texture2D>(tex2d_desc);
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

		aerial_perspective_lut = std::make_unique<Texture3D>(tex3d_desc);
		aerial_perspective_lut->CreateSRV(srv_desc);
		aerial_perspective_lut->CreateUAV(uav_desc);
	}

	CompileComputeShaders();
}

void PhysicalSky::CompileComputeShaders()
{
	logger::debug("Compiling shaders...");
	{
		auto program_ptr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(
			L"Data\\Shaders\\PhysicalSky\\LUTGen.cs.hlsl", { { "LUTGEN", "0" } }, "cs_5_0"));
		if (program_ptr)
			transmittance_program.attach(program_ptr);

		program_ptr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(
			L"Data\\Shaders\\PhysicalSky\\LUTGen.cs.hlsl", { { "LUTGEN", "1" } }, "cs_5_0"));
		if (program_ptr)
			multiscatter_program.attach(program_ptr);

		program_ptr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(
			L"Data\\Shaders\\PhysicalSky\\LUTGen.cs.hlsl", { { "LUTGEN", "2" } }, "cs_5_0"));
		if (program_ptr)
			sky_view_program.attach(program_ptr);

		program_ptr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(
			L"Data\\Shaders\\PhysicalSky\\LUTGen.cs.hlsl", { { "LUTGEN", "3" } }, "cs_5_0"));
		if (program_ptr)
			aerial_perspective_program.attach(program_ptr);
	}
}

void PhysicalSky::ClearShaderCache()
{
	auto checkClear = [](winrt::com_ptr<ID3D11ComputeShader>& shader) {
		if (shader) {
			shader->Release();
			shader.detach();
		}
	};

	checkClear(transmittance_program);
	checkClear(multiscatter_program);
	checkClear(sky_view_program);
	checkClear(aerial_perspective_program);

	CompileComputeShaders();
}

bool PhysicalSky::NeedLutsUpdate()
{
	return (RE::Sky::GetSingleton()->mode.get() == RE::Sky::Mode::kFull) &&
	       RE::Sky::GetSingleton()->currentClimate &&
	       CheckComputeShaders();
}

void PhysicalSky::Reset()
{
	UpdateBuffer();
}

void PhysicalSky::Draw(const RE::BSShader* shader, const uint32_t descriptor)
{
	static Util::FrameChecker frame_checker;
	if (frame_checker.isNewFrame() && phys_sky_sb_data.enable_sky)
		GenerateLuts();

	switch (shader->shaderType.get()) {
	case RE::BSShader::Type::Lighting:
	case RE::BSShader::Type::Grass:
	case RE::BSShader::Type::DistantTree:
		ModifyLighting();
		break;
	case RE::BSShader::Type::Sky:
		ModifySky(shader, descriptor);
		break;
	default:
		break;
	}
}

void PhysicalSky::GenerateLuts()
{
	auto context = State::GetSingleton()->context;

	/* ---- BACKUP ---- */
	struct ShaderState
	{
		ID3D11ShaderResourceView* srvs[4] = { nullptr };
		ID3D11ComputeShader* shader = nullptr;
		ID3D11Buffer* buffer = nullptr;
		ID3D11UnorderedAccessView* uavs[1] = { nullptr };
		ID3D11ClassInstance* instance = nullptr;
		ID3D11SamplerState* samplers[2] = { nullptr };
		UINT numInstances;
	} old, newer;
	context->CSGetShaderResources(0, ARRAYSIZE(old.srvs), old.srvs);
	context->CSGetShader(&old.shader, &old.instance, &old.numInstances);
	context->CSGetConstantBuffers(0, 1, &old.buffer);
	context->CSGetUnorderedAccessViews(0, ARRAYSIZE(old.uavs), old.uavs);
	context->CSGetSamplers(3, ARRAYSIZE(old.samplers), old.samplers);

	/* ---- DISPATCH ---- */
	newer.srvs[0] = phys_sky_sb->SRV(0);
	context->CSSetShaderResources(0, ARRAYSIZE(newer.srvs), newer.srvs);

	newer.samplers[0] = transmittance_sampler.get();
	newer.samplers[1] = sky_view_sampler.get();
	context->CSSetSamplers(3, ARRAYSIZE(newer.samplers), newer.samplers);

	// -> transmittance
	newer.uavs[0] = transmittance_lut->uav.get();
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(newer.uavs), newer.uavs, nullptr);
	context->CSSetShader(transmittance_program.get(), nullptr, 0);
	context->Dispatch(((s_transmittance_width - 1) >> 5) + 1, ((s_transmittance_height - 1) >> 5) + 1, 1);

	// -> multiscatter
	newer.uavs[0] = multiscatter_lut->uav.get();
	newer.srvs[1] = transmittance_lut->srv.get();
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(newer.uavs), newer.uavs, nullptr);
	context->CSSetShaderResources(0, ARRAYSIZE(newer.srvs), newer.srvs);
	context->CSSetShader(multiscatter_program.get(), nullptr, 0);
	context->Dispatch(((s_multiscatter_width - 1) >> 5) + 1, ((s_multiscatter_height - 1) >> 5) + 1, 1);

	// -> sky-view
	newer.uavs[0] = sky_view_lut->uav.get();
	newer.srvs[2] = multiscatter_lut->srv.get();
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(newer.uavs), newer.uavs, nullptr);
	context->CSSetShaderResources(0, ARRAYSIZE(newer.srvs), newer.srvs);
	context->CSSetShader(sky_view_program.get(), nullptr, 0);
	context->Dispatch(((s_sky_view_width - 1) >> 5) + 1, ((s_sky_view_height - 1) >> 5) + 1, 1);

	// -> aerial perspective
	if (settings.enable_aerial) {
		newer.uavs[0] = aerial_perspective_lut->uav.get();
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(newer.uavs), newer.uavs, nullptr);
		context->CSSetShader(aerial_perspective_program.get(), nullptr, 0);
		context->Dispatch(((s_aerial_perspective_width - 1) >> 5) + 1, ((s_aerial_perspective_height - 1) >> 5) + 1, 1);
	}

	/* ---- RESTORE ---- */
	context->CSSetShaderResources(0, ARRAYSIZE(old.srvs), old.srvs);
	context->CSSetShader(old.shader, &old.instance, old.numInstances);
	context->CSSetConstantBuffers(0, 1, &old.buffer);
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(old.uavs), old.uavs, nullptr);
	context->CSSetSamplers(3, ARRAYSIZE(old.samplers), old.samplers);
}

void PhysicalSky::ModifyLighting()
{
	auto context = State::GetSingleton()->context;

	ID3D11ShaderResourceView* views[5] = {
		phys_sky_sb->SRV(0),
		transmittance_lut->srv.get(),
		multiscatter_lut->srv.get(),
		sky_view_lut->srv.get(),
		aerial_perspective_lut->srv.get()
	};
	context->PSSetShaderResources(50, ARRAYSIZE(views), views);
}

void PhysicalSky::ModifySky(const RE::BSShader*, const uint32_t)
{
	auto context = State::GetSingleton()->context;

	SkyPerGeometrySB data = { .sky_object_type = current_sky_obj_type };
	sky_per_geo_sb->Update(&data, sizeof(data));

	ID3D11SamplerState* samplers[2] = {
		transmittance_sampler.get(),
		sky_view_sampler.get()
	};
	context->PSSetSamplers(3, ARRAYSIZE(samplers), samplers);

	ID3D11ShaderResourceView* srvs[8] = {
		phys_sky_sb->SRV(0),
		transmittance_lut->srv.get(),
		multiscatter_lut->srv.get(),
		sky_view_lut->srv.get(),
		aerial_perspective_lut->srv.get(),
		sky_per_geo_sb->SRV(0),
		nullptr,
		nullptr
	};

	auto sky = RE::Sky::GetSingleton();
	if (auto masser = sky->masser) {
		RE::NiTexturePtr masser_tex;
		RE::BSShaderManager::GetTexture(masser->stateTextures[current_moon_phases[0]].c_str(), true, masser_tex, false);  // TODO: find the phase
		if (masser_tex)
			srvs[6] = reinterpret_cast<RE::NiSourceTexture*>(masser_tex.get())->rendererTexture->resourceView;
	}
	if (auto secunda = sky->secunda) {
		RE::NiTexturePtr secunda_tex;
		RE::BSShaderManager::GetTexture(secunda->stateTextures[current_moon_phases[1]].c_str(), true, secunda_tex, false);
		if (secunda_tex)
			srvs[7] = reinterpret_cast<RE::NiSourceTexture*>(secunda_tex.get())->rendererTexture->resourceView;
	}

	context->PSSetShaderResources(50, ARRAYSIZE(srvs), srvs);
}