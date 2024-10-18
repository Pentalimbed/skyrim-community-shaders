#include "../PhysicalSky.h"

#include "Deferred.h"
#include "State.h"
#include "Util.h"

#include "../TerrainShadows.h"

bool PhysicalSky::HasShaderDefine(RE::BSShader::Type type)
{
	switch (type) {
	case RE::BSShader::Type::Sky:
	case RE::BSShader::Type::Lighting:
	case RE::BSShader::Type::Grass:
	case RE::BSShader::Type::DistantTree:
	case RE::BSShader::Type::Effect:
	case RE::BSShader::Type::Water:
		return true;
		break;
	default:
		return false;
		break;
	}
}

void PhysicalSky::SetupResources()
{
	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
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
		phys_sky_sb = eastl::make_unique<StructuredBuffer>(StructuredBufferDesc<PhysSkySB>(), 1);
		phys_sky_sb->CreateSRV();
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

	logger::debug("Creating render textures...");
	{
		D3D11_TEXTURE2D_DESC texDesc;
		auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		mainTex.texture->GetDesc(&texDesc);
		texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;
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

		{
			main_view_tr_tex = eastl::make_unique<Texture2D>(texDesc);
			main_view_tr_tex->CreateSRV(srvDesc);
			main_view_tr_tex->CreateUAV(uavDesc);

			main_view_lum_tex = eastl::make_unique<Texture2D>(texDesc);
			main_view_lum_tex->CreateSRV(srvDesc);
			main_view_lum_tex->CreateUAV(uavDesc);
		}
	}

	CompileComputeShaders();
}

void PhysicalSky::CompileComputeShaders()
{
	struct ShaderCompileInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* csPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines;
	};

	std::vector<ShaderCompileInfo> shaderInfos = {
		{ &transmittance_program, "LUTGen.cs.hlsl", { { "LUTGEN", "0" } } },
		{ &multiscatter_program, "LUTGen.cs.hlsl", { { "LUTGEN", "1" } } },
		{ &sky_view_program, "LUTGen.cs.hlsl", { { "LUTGEN", "2" } } },
		{ &aerial_perspective_program, "LUTGen.cs.hlsl", { { "LUTGEN", "3" } } },
		{ &main_view_program, "Volumetrics.cs.hlsl" }
	};

	for (auto& info : shaderInfos) {
		auto path = std::filesystem::path("Data\\Shaders\\PhysicalSky") / info.filename;
		if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0")))
			info.csPtr->attach(rawPtr);
	}
}

void PhysicalSky::ClearShaderCache()
{
	const auto shaderPtrs = std::array{
		&transmittance_program, &multiscatter_program, &sky_view_program, &aerial_perspective_program, &main_view_program
	};

	for (auto shader : shaderPtrs)
		*shader = nullptr;

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

void PhysicalSky::Prepass()
{
	if (phys_sky_sb_data.enable_sky) {
		GenerateLuts();
		RenderMainView();
	} else {
		auto context = State::GetSingleton()->context;
		{
			FLOAT clr[4] = { 1., 1., 1., 1. };
			context->ClearUnorderedAccessViewFloat(main_view_tr_tex->uav.get(), clr);
		}
		{
			FLOAT clr[4] = { 0., 0., 0., 0. };
			context->ClearUnorderedAccessViewFloat(main_view_lum_tex->uav.get(), clr);
		}
	}

	auto context = State::GetSingleton()->context;

	std::array<ID3D11ShaderResourceView*, 7> srvs = {
		phys_sky_sb->SRV(0),
		transmittance_lut->srv.get(),
		multiscatter_lut->srv.get(),
		sky_view_lut->srv.get(),
		aerial_perspective_lut->srv.get(),
		nullptr,
		nullptr
	};

	if (phys_sky_sb_data.enable_sky) {
		auto sky = RE::Sky::GetSingleton();
		if (auto masser = sky->masser) {
			RE::NiTexturePtr masser_tex;
			RE::BSShaderManager::GetTexture(masser->stateTextures[current_moon_phases[0]].c_str(), true, masser_tex, false);  // TODO: find the phase
			if (masser_tex)
				srvs.at(5) = reinterpret_cast<RE::NiSourceTexture*>(masser_tex.get())->rendererTexture->resourceView;
		}
		if (auto secunda = sky->secunda) {
			RE::NiTexturePtr secunda_tex;
			RE::BSShaderManager::GetTexture(secunda->stateTextures[current_moon_phases[1]].c_str(), true, secunda_tex, false);
			if (secunda_tex)
				srvs.at(6) = reinterpret_cast<RE::NiSourceTexture*>(secunda_tex.get())->rendererTexture->resourceView;
		}
	}

	context->PSSetShaderResources(100, (uint)srvs.size(), srvs.data());

	if (phys_sky_sb_data.enable_sky) {
		ID3D11SamplerState* samplers[2] = {
			transmittance_sampler.get(),
			sky_view_sampler.get()
		};
		context->PSSetSamplers(3, ARRAYSIZE(samplers), samplers);
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
	newer.uavs[0] = aerial_perspective_lut->uav.get();
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(newer.uavs), newer.uavs, nullptr);
	context->CSSetShader(aerial_perspective_program.get(), nullptr, 0);
	context->Dispatch(((s_aerial_perspective_width - 1) >> 5) + 1, ((s_aerial_perspective_height - 1) >> 5) + 1, 1);

	/* ---- RESTORE ---- */
	context->CSSetShaderResources(0, ARRAYSIZE(old.srvs), old.srvs);
	context->CSSetShader(old.shader, &old.instance, old.numInstances);
	context->CSSetConstantBuffers(0, 1, &old.buffer);
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(old.uavs), old.uavs, nullptr);
	context->CSSetSamplers(3, ARRAYSIZE(old.samplers), old.samplers);
}

void PhysicalSky::RenderMainView()
{
	auto& context = State::GetSingleton()->context;
	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	auto deferred = Deferred::GetSingleton();

	float2 size = Util::ConvertToDynamic(State::GetSingleton()->screenSize);
	uint resolution[2] = { (uint)size.x, (uint)size.y };

	std::array<ID3D11ShaderResourceView*, 8> srvs = {
		phys_sky_sb->SRV(0),
		transmittance_lut->srv.get(),
		multiscatter_lut->srv.get(),
		aerial_perspective_lut->srv.get(),
		renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY].depthSRV,
		deferred->shadowView,
		deferred->perShadow->srv.get(),
		TerrainShadows::GetSingleton()->IsHeightMapReady() ? TerrainShadows::GetSingleton()->texShadowHeight->srv.get() : nullptr,
	};
	std::array<ID3D11UnorderedAccessView*, 2> uavs = { main_view_tr_tex->uav.get(), main_view_lum_tex->uav.get() };
	std::array<ID3D11SamplerState*, 2> samplers = { transmittance_sampler.get(), sky_view_sampler.get() };

	context->CSSetSamplers(3, (uint)samplers.size(), samplers.data());

	context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
	context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	context->CSSetShader(main_view_program.get(), nullptr, 0);
	context->Dispatch((resolution[0] + 7u) >> 3, (resolution[1] + 7u) >> 3, 1);

	samplers.fill(nullptr);
	srvs.fill(nullptr);
	uavs.fill(nullptr);
	context->CSSetShaderResources(0, (uint)srvs.size(), srvs.data());
	context->CSSetUnorderedAccessViews(0, (uint)uavs.size(), uavs.data(), nullptr);
	context->CSSetSamplers(3, (uint)samplers.size(), samplers.data());
	context->CSSetShader(nullptr, nullptr, 0);
}