#include "../PhysicalWeather.h"
#include "PhysicalWeather_Common.h"

void PhysicalWeather::SetupResources()
{
	if (!loaded)
		return;
	try {
		logger::debug("Creating structured buffers...");
		{
			D3D11_BUFFER_DESC sb_desc{
				.ByteWidth = sizeof(PhysWeatherSB),
				.Usage = D3D11_USAGE_DYNAMIC,
				.BindFlags = D3D11_BIND_SHADER_RESOURCE,
				.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
				.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
				.StructureByteStride = sizeof(PhysWeatherSB)
			};
			D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{
				.Format = DXGI_FORMAT_UNKNOWN,
				.ViewDimension = D3D11_SRV_DIMENSION_BUFFER,
				.Buffer = { .FirstElement = 0, .NumElements = 1 }
			};

			phys_weather_sb = std::make_unique<Buffer>(sb_desc);
			phys_weather_sb->CreateSRV(srv_desc);
		}

		logger::debug("Creating noise textures...");
		{
			D3D11_TEXTURE3D_DESC tex3d_desc{
				.Width = s_noise_size,
				.Height = s_noise_size,
				.Depth = s_noise_size,
				.MipLevels = 1,
				.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
				.Usage = D3D11_USAGE_DEFAULT,
				.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET,
				.CPUAccessFlags = 0,
				.MiscFlags = 0
			};
			D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {
				.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
				.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D,
				.Texture3D = { .MostDetailedMip = 0, .MipLevels = 1 }
			};
			D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {
				.Format = DXGI_FORMAT_R16G16B16A16_FLOAT,
				.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D,
				.Texture3D = { .MipSlice = 0, .FirstWSlice = 0, .WSize = s_noise_size }
			};

			noise_tex = std::make_unique<Texture3D>(tex3d_desc);
			noise_tex->CreateSRV(srv_desc);
			noise_tex->CreateUAV(uav_desc);
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

		CompileShaders();
	} catch (DX::com_exception& e) {
		logger::error("Error during resource setup:\n{}", e.what());
		throw e;
	}
}

void PhysicalWeather::CompileShaders()
{
	logger::debug("Compiling shaders...");
	{
		auto noisegen_program_ptr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\PhysicalWeather\\noisegen.cs.hlsl", {}, "cs_5_0"));
		if (noisegen_program_ptr)
			noisegen_program.attach(noisegen_program_ptr);

		auto transmittance_program_ptr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\PhysicalWeather\\transmittance.cs.hlsl", {}, "cs_5_0"));
		if (transmittance_program_ptr)
			transmittance_program.attach(transmittance_program_ptr);

		auto multiscatter_program_ptr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\PhysicalWeather\\multiscatter.cs.hlsl", {}, "cs_5_0"));
		if (multiscatter_program_ptr)
			multiscatter_program.attach(multiscatter_program_ptr);

		auto sky_view_program_ptr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\PhysicalWeather\\finalmarch.cs.hlsl", { { "SKY_VIEW", "" } }, "cs_5_0"));
		if (sky_view_program_ptr)
			sky_view_program.attach(sky_view_program_ptr);

		auto aerial_perspective_program_ptr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\PhysicalWeather\\finalmarch.cs.hlsl", {}, "cs_5_0"));
		if (aerial_perspective_program_ptr)
			aerial_perspective_program.attach(aerial_perspective_program_ptr);
	}
}

void PhysicalWeather::RecompileShaders()
{
	transmittance_program->Release();
	multiscatter_program->Release();
	sky_view_program->Release();
	aerial_perspective_program->Release();
	CompileShaders();
}

void PhysicalWeather::Draw(const RE::BSShader* shader, [[maybe_unused]] const uint32_t descriptor)
{
	if (!loaded)
		return;

	Update();
	GenerateNoise();
	if (phys_weather_sb_content.enable_sky)
		GenerateLuts();

	switch (shader->shaderType.get()) {
	case RE::BSShader::Type::Sky:
		ModifySky(shader, descriptor);
		break;
	case RE::BSShader::Type::Lighting:
		ModifyLighting();
		break;
	default:
		break;
	}
}

void PhysicalWeather::UploadPhysWeatherSB()
{
	auto context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;
	D3D11_MAPPED_SUBRESOURCE mapped;
	DX::ThrowIfFailed(context->Map(phys_weather_sb->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
	memcpy_s(mapped.pData, sizeof(PhysWeatherSB), &phys_weather_sb_content, sizeof(PhysWeatherSB));
	context->Unmap(phys_weather_sb->resource.get(), 0);
}

void PhysicalWeather::GenerateNoise()
{
	static std::once_flag flag;
	std::call_once(flag, [&]() {
		auto context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;

		/* ---- BACKUP ---- */
		struct OldState
		{
			ID3D11UnorderedAccessView* uav;
		} old;
		context->CSGetUnorderedAccessViews(0, 1, &old.uav);

		/* ---- DISPATCH ---- */
		context->CSSetUnorderedAccessViews(0, 1, noise_tex->uav.put(), nullptr);
		context->CSSetShader(noisegen_program.get(), nullptr, 0);
		uint16_t dispsize = ((s_noise_size - 1) >> 3) + 1;
		context->Dispatch(dispsize, dispsize, dispsize);

		/* ---- RESTORE ---- */
		context->CSSetUnorderedAccessViews(0, 1, &old.uav, nullptr);
		if (old.uav)
			old.uav->Release();
	});
}

void PhysicalWeather::GenerateLuts()
{
	static FrameChecker frame_checker;
	if (!frame_checker.isNewFrame())
		return;

	auto context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;

	/* ---- BACKUP ---- */
	struct OldState
	{
		ID3D11ShaderResourceView* srvs[4];
		ID3D11ComputeShader* shader;
		ID3D11Buffer* buffer;
		ID3D11UnorderedAccessView* uav;
		ID3D11ClassInstance* instance;
		UINT numInstances;
	} old;
	context->CSGetShaderResources(0, ARRAYSIZE(old.srvs), old.srvs);
	context->CSGetShader(&old.shader, &old.instance, &old.numInstances);
	context->CSGetConstantBuffers(0, 1, &old.buffer);
	context->CSGetUnorderedAccessViews(0, 1, &old.uav);

	/* ---- DISPATCH ---- */
	context->CSSetShaderResources(0, 1, phys_weather_sb->srv.put());

	// -> transmittance
	context->CSSetUnorderedAccessViews(0, 1, transmittance_lut->uav.put(), nullptr);
	context->CSSetShader(transmittance_program.get(), nullptr, 0);
	context->Dispatch(((s_transmittance_width - 1) >> 5) + 1, ((s_transmittance_height - 1) >> 5) + 1, 1);

	// -> multiscatter
	context->CSSetUnorderedAccessViews(0, 1, multiscatter_lut->uav.put(), nullptr);
	context->CSSetShaderResources(1, 1, transmittance_lut->srv.put());
	context->CSSetShader(multiscatter_program.get(), nullptr, 0);
	context->Dispatch(((s_multiscatter_width - 1) >> 5) + 1, ((s_multiscatter_height - 1) >> 5) + 1, 1);

	// -> sky-view
	context->CSSetUnorderedAccessViews(0, 1, sky_view_lut->uav.put(), nullptr);
	context->CSSetShaderResources(2, 1, multiscatter_lut->srv.put());
	context->CSSetShader(sky_view_program.get(), nullptr, 0);
	context->Dispatch(((s_sky_view_width - 1) >> 5) + 1, ((s_sky_view_height - 1) >> 5) + 1, 1);

	// -> aerial perspective
	if (phys_weather_sb_content.enable_scatter) {
		context->CSSetUnorderedAccessViews(0, 1, aerial_perspective_lut->uav.put(), nullptr);
		context->CSSetShader(aerial_perspective_program.get(), nullptr, 0);
		context->Dispatch(((s_aerial_perspective_width - 1) >> 5) + 1, ((s_aerial_perspective_height - 1) >> 5) + 1, 1);
	}

	/* ---- RESTORE ---- */
	context->CSSetShaderResources(0, ARRAYSIZE(old.srvs), old.srvs);
	for (uint8_t i = 0; i < ARRAYSIZE(old.srvs); i++)
		if (old.srvs[i])
			old.srvs[i]->Release();
	context->CSSetShader(old.shader, &old.instance, old.numInstances);
	if (old.shader)
		old.shader->Release();
	context->CSSetConstantBuffers(0, 1, &old.buffer);
	if (old.buffer)
		old.buffer->Release();
	context->CSSetUnorderedAccessViews(0, 1, &old.uav, nullptr);
	if (old.uav)
		old.uav->Release();
}

void PhysicalWeather::ModifyLighting()
{
	auto context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;

	context->PSSetShaderResources(16, 1, aerial_perspective_lut->srv.put());
	context->PSSetShaderResources(17, 1, transmittance_lut->srv.put());
	context->PSSetShaderResources(18, 1, phys_weather_sb->srv.put());
}

void PhysicalWeather::ModifySky(const RE::BSShader*, const uint32_t descriptor)
{
	enum class SkyShaderTechniques
	{
		SunOcclude = 0,
		SunGlare = 1,
		MoonAndStarsMask = 2,
		Stars = 3,
		Clouds = 4,
		CloudsLerp = 5,
		CloudsFade = 6,
		Texture = 7,
		Sky = 8,
	};

	auto context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;
	auto tech_enum = static_cast<SkyShaderTechniques>(descriptor);

	if (tech_enum != SkyShaderTechniques::Sky)
		return;

	context->PSSetShaderResources(16, 1, phys_weather_sb->srv.put());
	context->PSSetShaderResources(17, 1, sky_view_lut->srv.put());
	context->PSSetShaderResources(18, 1, aerial_perspective_lut->srv.put());
	context->PSSetShaderResources(19, 1, transmittance_lut->srv.put());

	auto sky = RE::Sky::GetSingleton();
	auto masser = sky->masser;
	auto secunda = sky->secunda;
	if (masser) {
		RE::NiSourceTexturePtr masser_tex;
		Hooks::BSShaderManager_GetTexture::func(masser->stateTextures[RE::Moon::Phase::kFull].c_str(), true, masser_tex, false);  // TODO: find the phase
		if (masser_tex)
			context->PSSetShaderResources(20, 1, &masser_tex->rendererTexture->m_ResourceView);
	}
	if (secunda) {
		RE::NiSourceTexturePtr secunda_tex;
		Hooks::BSShaderManager_GetTexture::func(secunda->stateTextures[RE::Moon::Phase::kFull].c_str(), true, secunda_tex, false);
		if (secunda_tex)
			context->PSSetShaderResources(21, 1, &secunda_tex->rendererTexture->m_ResourceView);
	}
}
