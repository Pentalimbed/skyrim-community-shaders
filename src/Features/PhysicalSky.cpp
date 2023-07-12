#include "PhysicalSky.h"

#include <implot.h>

#include "Hooks.h"
#include "ShaderTools/BSGraphicsTypes.h"
#include "Util.h"

#pragma region MISC
constexpr auto hdr_color_edit_flags = ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR;

class FrameChecker
{
private:
	uint32_t last_frame = UINT32_MAX;

public:
	inline bool isNewFrame(uint32_t frame)
	{
		bool retval = last_frame != frame;
		last_frame = frame;
		return retval;
	}
};
#pragma endregion MISC

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PhysicalSky::Settings,
	enable_sky,
	enable_scatter,
	transmittance_step,
	multiscatter_step,
	multiscatter_sqrt_samples,
	skyview_step,
	aerial_perspective_max_dist, unit_scale,
	bottom_z,
	ground_radius,
	atmos_thickness, ground_albedo,
	sun_color,
	limb_darken_model,
	limb_darken_power,
	sun_color,
	sun_aperture_angle,
	masser_aperture_angle,
	secunda_aperture_angle,
	rayleigh_scatter,
	rayleigh_absorption,
	rayleigh_decay,
	mie_scatter,
	mie_absorption,
	mie_decay,
	ozone_absorption,
	ozone_height,
	ozone_thickness,
	ap_inscatter_mix,
	ap_transmittance_mix,
	light_transmittance_mix)

#pragma region SETUP
void PhysicalSky::SetupResources()
{
	if (!loaded)
		return;
	try {
		logger::debug("Creating structured buffers...");
		{
			D3D11_BUFFER_DESC sb_desc{
				.ByteWidth = sizeof(PhysSkySB),
				.Usage = D3D11_USAGE_DYNAMIC,
				.BindFlags = D3D11_BIND_SHADER_RESOURCE,
				.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
				.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
				.StructureByteStride = sizeof(PhysSkySB)
			};
			D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{
				.Format = DXGI_FORMAT_UNKNOWN,
				.ViewDimension = D3D11_SRV_DIMENSION_BUFFER,
				.Buffer = { .FirstElement = 0, .NumElements = 1 }
			};

			phys_sky_sb = std::make_unique<Buffer>(sb_desc);
			phys_sky_sb->CreateSRV(srv_desc);
		}

		logger::debug("Creating sampler...");
		{
			auto renderer = RE::BSGraphics::Renderer::GetSingleton();
			auto device = renderer->GetRuntimeData().forwarder;

			D3D11_SAMPLER_DESC sampler_desc = {};
			sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			sampler_desc.MaxAnisotropy = 1;
			sampler_desc.MinLOD = 0;
			sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
			DX::ThrowIfFailed(device->CreateSamplerState(&sampler_desc, common_clamp_sampler.put()));
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

void PhysicalSky::CompileShaders()
{
	logger::debug("Compiling shaders...");
	{
		auto transmittance_program_ptr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\PhysicalSky\\transmittance.cs.hlsl", {}, "cs_5_0"));
		if (transmittance_program_ptr)
			transmittance_program.attach(transmittance_program_ptr);

		auto multiscatter_program_ptr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\PhysicalSky\\multiscatter.cs.hlsl", {}, "cs_5_0"));
		if (multiscatter_program_ptr)
			multiscatter_program.attach(multiscatter_program_ptr);

		auto sky_view_program_ptr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\PhysicalSky\\finalmarch.cs.hlsl", { { "SKY_VIEW", "" } }, "cs_5_0"));
		if (sky_view_program_ptr)
			sky_view_program.attach(sky_view_program_ptr);

		auto aerial_perspective_program_ptr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\PhysicalSky\\finalmarch.cs.hlsl", {}, "cs_5_0"));
		if (aerial_perspective_program_ptr)
			aerial_perspective_program.attach(aerial_perspective_program_ptr);
	}
}

void PhysicalSky::RecompileShaders()
{
	transmittance_program->Release();
	multiscatter_program->Release();
	sky_view_program->Release();
	aerial_perspective_program->Release();
	CompileShaders();
}
#pragma endregion SETUP

#pragma region DRAW
void PhysicalSky::Draw(const RE::BSShader* shader, [[maybe_unused]] const uint32_t descriptor)
{
	if (!loaded)
		return;

	UpdatePhysSkySB();
	if (phys_sky_sb_content.enable_sky)
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

void PhysicalSky::UpdatePhysSkySB()
{
	static FrameChecker frame_checker;
	if (!frame_checker.isNewFrame(RE::BSGraphics::State::GetSingleton()->uiFrameCount))
		return;

	phys_sky_sb_content = {
		.enable_sky = settings.enable_sky && (RE::Sky::GetSingleton()->mode.get() == RE::Sky::Mode::kFull),
		.enable_scatter = settings.enable_scatter,

		.transmittance_step = settings.transmittance_step,
		.multiscatter_step = settings.multiscatter_step,
		.multiscatter_sqrt_samples = settings.multiscatter_sqrt_samples,
		.skyview_step = settings.skyview_step,
		.aerial_perspective_max_dist = settings.aerial_perspective_max_dist,

		.unit_scale = settings.unit_scale,
		.bottom_z = settings.bottom_z,
		.ground_radius = settings.ground_radius,
		.atmos_thickness = settings.atmos_thickness,
		.ground_albedo = settings.ground_albedo,

		.light_color = settings.light_color,

		.limb_darken_model = settings.limb_darken_model,
		.limb_darken_power = settings.limb_darken_power,
		.sun_color = settings.sun_color,
		.sun_aperture_cos = cos(settings.sun_aperture_angle),

		.masser_aperture_cos = cos(settings.masser_aperture_angle),
		.masser_brightness = settings.masser_brightness,

		.secunda_aperture_cos = cos(settings.secunda_aperture_angle),
		.secunda_brightness = settings.secunda_brightness,

		.rayleigh_scatter = settings.rayleigh_scatter,
		.rayleigh_absorption = settings.rayleigh_absorption,
		.rayleigh_decay = settings.rayleigh_decay,

		.mie_scatter = settings.mie_scatter,
		.mie_absorption = settings.mie_absorption,
		.mie_decay = settings.mie_decay,

		.ozone_absorption = settings.ozone_absorption,
		.ozone_height = settings.ozone_height,
		.ozone_thickness = settings.ozone_thickness,

		.ap_inscatter_mix = settings.ap_inscatter_mix,
		.ap_transmittance_mix = settings.ap_transmittance_mix,
		.light_transmittance_mix = settings.light_transmittance_mix
	};

	// dynamic variables
	static uint32_t custom_timer = 0;
	custom_timer += uint32_t(RE::GetSecondsSinceLastFrame() * 1e3f);
	phys_sky_sb_content.timer = custom_timer * 1e-3f;

	auto accumulator = RE::BSGraphics::BSShaderAccumulator::GetCurrentAccumulator();
	auto dir_light = skyrim_cast<RE::NiDirectionalLight*>(accumulator->GetRuntimeData().activeShadowSceneNode->GetRuntimeData().sunLight->light.get());
	if (dir_light) {
		auto sun_dir = -dir_light->GetWorldDirection();
		phys_sky_sb_content.sun_dir = { sun_dir.x, sun_dir.y, sun_dir.z };
	}

	RE::NiPoint3 cam_pos = { 0, 0, 0 };
	if (auto cam = RE::PlayerCamera::GetSingleton(); cam && cam->cameraRoot) {
		cam_pos = cam->cameraRoot->world.translate;
		phys_sky_sb_content.player_cam_pos = { cam_pos.x, cam_pos.y, cam_pos.z };
	}

	auto sky = RE::Sky::GetSingleton();
	// auto sun = sky->sun;
	auto masser = sky->masser;
	auto secunda = sky->secunda;
	// if (sun) {
	// 	auto sun_dir = sun->sunBase->world.translate - cam_pos;
	// 	sun_dir.Unitize();
	// 	phys_sky_sb_content.sun_dir = { sun_dir.x, sun_dir.y, sun_dir.z };
	// } // during night it flips
	if (masser) {
		auto masser_dir = masser->moonMesh->world.translate - cam_pos;
		masser_dir.Unitize();
		auto masser_upvec = masser->moonMesh->world.rotate * RE::NiPoint3{ 0, 1, 0 };

		phys_sky_sb_content.masser_dir = { masser_dir.x, masser_dir.y, masser_dir.z };
		phys_sky_sb_content.masser_upvec = { masser_upvec.x, masser_upvec.y, masser_upvec.z };
	}
	if (secunda) {
		auto secunda_dir = secunda->moonMesh->world.translate - cam_pos;
		secunda_dir.Unitize();
		auto secunda_upvec = secunda->moonMesh->world.rotate * RE::NiPoint3{ 0, 1, 0 };

		phys_sky_sb_content.secunda_dir = { secunda_dir.x, secunda_dir.y, secunda_dir.z };
		phys_sky_sb_content.secunda_upvec = { secunda_upvec.x, secunda_upvec.y, secunda_upvec.z };
	}
}

void PhysicalSky::UploadPhysSkySB()
{
	auto context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;
	D3D11_MAPPED_SUBRESOURCE mapped;
	DX::ThrowIfFailed(context->Map(phys_sky_sb->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
	memcpy_s(mapped.pData, sizeof(PhysSkySB), &phys_sky_sb_content, sizeof(PhysSkySB));
	context->Unmap(phys_sky_sb->resource.get(), 0);
}

void PhysicalSky::GenerateLuts()
{
	static FrameChecker frame_checker;
	if (!frame_checker.isNewFrame(RE::BSGraphics::State::GetSingleton()->uiFrameCount))
		return;

	UploadPhysSkySB();

	auto context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;

	/* ---- BACKUP ---- */
	struct OldState
	{
		ID3D11ShaderResourceView* srvs[4];
		ID3D11SamplerState* sampler;
		ID3D11ComputeShader* shader;
		ID3D11Buffer* buffer;
		ID3D11UnorderedAccessView* uav;
		ID3D11ClassInstance* instance;
		UINT numInstances;
	} old;
	context->CSGetShaderResources(0, ARRAYSIZE(old.srvs), old.srvs);
	context->CSGetSamplers(0, 1, &old.sampler);
	context->CSGetShader(&old.shader, &old.instance, &old.numInstances);
	context->CSGetConstantBuffers(0, 1, &old.buffer);
	context->CSGetUnorderedAccessViews(0, 1, &old.uav);

	/* ---- DISPATCH ---- */
	context->CSSetSamplers(0, 1, common_clamp_sampler.put());

	context->CSSetShaderResources(0, 1, phys_sky_sb->srv.put());

	ID3D11Buffer* pergeo_cb;
	context->VSGetConstantBuffers(2, 1, &pergeo_cb);
	context->CSSetConstantBuffers(0, 1, &pergeo_cb);

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
	if (phys_sky_sb_content.enable_scatter) {
		context->CSSetUnorderedAccessViews(0, 1, aerial_perspective_lut->uav.put(), nullptr);
		context->CSSetShader(aerial_perspective_program.get(), nullptr, 0);
		context->Dispatch(((s_aerial_perspective_width - 1) >> 5) + 1, ((s_aerial_perspective_height - 1) >> 5) + 1, 1);
	}

	/* ---- RESTORE ---- */
	context->CSSetShaderResources(0, ARRAYSIZE(old.srvs), old.srvs);
	for (uint8_t i = 0; i < ARRAYSIZE(old.srvs); i++)
		if (old.srvs[i])
			old.srvs[i]->Release();
	context->CSSetSamplers(0, 1, &old.sampler);
	if (old.sampler)
		old.sampler->Release();
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

void PhysicalSky::ModifyLighting()
{
	auto context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;

	context->PSSetShaderResources(16, 1, aerial_perspective_lut->srv.put());
	context->PSSetShaderResources(17, 1, transmittance_lut->srv.put());
	context->PSSetShaderResources(18, 1, phys_sky_sb->srv.put());
}

void PhysicalSky::ModifySky(const RE::BSShader*, const uint32_t descriptor)
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

	if (descriptor != std::underlying_type_t<SkyShaderTechniques>(SkyShaderTechniques::Sky))
		return;

	context->PSSetShaderResources(16, 1, phys_sky_sb->srv.put());
	context->PSSetShaderResources(17, 1, sky_view_lut->srv.put());
	context->PSSetShaderResources(18, 1, transmittance_lut->srv.put());

	auto sky = RE::Sky::GetSingleton();
	auto masser = sky->masser;
	auto secunda = sky->secunda;
	if (masser) {
		RE::NiSourceTexturePtr masser_tex;
		Hooks::BSShaderManager_GetTexture::func(masser->stateTextures[RE::Moon::Phase::kFull].c_str(), true, masser_tex, false);  // TODO: find the phase
		if (masser_tex)
			context->PSSetShaderResources(19, 1, &masser_tex->rendererTexture->m_ResourceView);
	}
	if (secunda) {
		RE::NiSourceTexturePtr secunda_tex;
		Hooks::BSShaderManager_GetTexture::func(secunda->stateTextures[RE::Moon::Phase::kFull].c_str(), true, secunda_tex, false);
		if (secunda_tex)
			context->PSSetShaderResources(20, 1, &secunda_tex->rendererTexture->m_ResourceView);
	}
}
#pragma endregion DRAW

#pragma region IMGUI
void PhysicalSky::DrawSettings()
{
	static int pagenum = 0;

	ImGui::Combo("Page", &pagenum, "General\0Quality\0World\0Atmosphere\0Celestials\0Debug\0");

	ImGui::Separator();

	switch (pagenum) {
	case 0:
		DrawSettingsGeneral();
		break;
	case 1:
		DrawSettingsQuality();
		break;
	case 2:
		DrawSettingsWorld();
		break;
	case 3:
		DrawSettingsAtmosphere();
		break;
	case 4:
		DrawSettingsCelestials();
		break;
	case 5:
		DrawSettingsDebug();
		break;
	default:
		break;
	}
}

void PhysicalSky::DrawSettingsGeneral()
{
	ImGui::Checkbox("Enable Physcial Sky", reinterpret_cast<bool*>(&settings.enable_sky));

	ImGui::Indent();
	{
		ImGui::Checkbox("Enable Aerial Perspective", reinterpret_cast<bool*>(&settings.enable_scatter));
	}
	ImGui::Unindent();
}

void PhysicalSky::DrawSettingsQuality()
{
	ImGui::Text("The bigger the settings below, the more accurate and more performance-heavy things are.");
	ImGui::DragScalar("Transmittance Steps", ImGuiDataType_U32, &settings.transmittance_step);
	ImGui::DragScalar("Multiscatter Steps", ImGuiDataType_U32, &settings.multiscatter_step);
	ImGui::DragScalar("Multiscatter Sqrt Samples", ImGuiDataType_U32, &settings.multiscatter_sqrt_samples);
	ImGui::DragScalar("Sky View Steps", ImGuiDataType_U32, &settings.skyview_step);
	ImGui::DragFloat("Aerial Perspective Max Dist", &settings.aerial_perspective_max_dist);
	ImGui::TreePop();
}

void PhysicalSky::DrawSettingsWorld()
{
	ImGui::SliderFloat2("Unit Scale", &settings.unit_scale.x, 0.1f, 50.f);
	ImGui::ColorEdit3("Ground Albedo", &settings.ground_albedo.x, hdr_color_edit_flags);

	ImGui::ColorEdit3("Light Color", &settings.light_color.x, hdr_color_edit_flags);
}

void PhysicalSky::DrawSettingsAtmosphere()
{
	if (ImGui::TreeNodeEx("Mixing", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("In-scatter Mix", &settings.ap_inscatter_mix, 0.f, 1.f);
		ImGui::SliderFloat("View Transmittance Mix", &settings.ap_transmittance_mix, 0.f, 1.f);
		ImGui::SliderFloat("Light Transmittance Mix", &settings.light_transmittance_mix, 0.f, 1.f);

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Participating Media", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TextWrapped(
			"Rayleigh scatter: scattering by particles smaller than the wavelength of light, like air molecules, "
			"generally isotropic. This what makes the sky blue, and red at sunset. Usually needs no change.");
		ImGui::ColorEdit3("Rayleigh Scatter", &settings.rayleigh_scatter.x, hdr_color_edit_flags);
		ImGui::ColorEdit3("Rayleigh Absorption", &settings.rayleigh_absorption.x, hdr_color_edit_flags);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Usually zero");
		ImGui::DragFloat("Rayleigh Height Decay", &settings.rayleigh_decay, .1f, 0.f, 100.f);

		ImGui::TextWrapped(
			"Mie scatter: scattering by particles similar to the wavelength of light, like dust particles, "
			"with strong forward scattering characteristics. This generates glow around the sun. Increase in dustier weather.");
		ImGui::ColorEdit3("Mie Scatter", &settings.mie_scatter.x, hdr_color_edit_flags);
		ImGui::ColorEdit3("Mie Absorption", &settings.mie_absorption.x, hdr_color_edit_flags);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("Usually 1/9 of scatter coefficient. Dust/pollution is lower, fog is higher");
		ImGui::DragFloat("Mie Height Decay", &settings.mie_decay, .1f, 0.f, 100.f);

		ImGui::TextWrapped(
			"Ozone absorption: light absorption by the high ozone layer. This keeps the zenith sky blue, especially at sunrise or sunset.");
		ImGui::ColorEdit3("Ozone Absorption", &settings.ozone_absorption.x, hdr_color_edit_flags);
		ImGui::DragFloat("Ozone Mean Height", &settings.ozone_height, .1f, 0.f, 100.f);
		ImGui::DragFloat("Ozone Layer Thickness", &settings.ozone_thickness, .1f, 0.f, 50.f);

		// if (ImPlot::BeginPlot("Media Density", { -1, 0 }, ImPlotFlags_NoInputs)) {
		// 	ImPlot::SetupAxis(ImAxis_X1, "Altitude / km");
		// 	ImPlot::SetupAxis(ImAxis_Y1, "Relative Density");
		// 	ImPlot::SetupLegend(ImPlotLocation_NorthEast);
		// 	ImPlot::SetupFinish();

		// 	constexpr size_t n_datapoints = 101;
		// 	std::array<float, n_datapoints> heights, rayleigh_data, mie_data, ozone_data;
		// 	for (size_t i = 0; i < n_datapoints; i++) {
		// 		heights[i] = phys_sky_sb_content.atmos_thickness * 1e3f * i / (n_datapoints - 1);
		// 		rayleigh_data[i] = exp(-heights[i] / settings.rayleigh_decay);
		// 		mie_data[i] = exp(-heights[i] / settings.mie_decay);
		// 		ozone_data[i] = max(0.f, 1 - abs(heights[i] - settings.ozone_height) / (settings.ozone_thickness * .5f));
		// 	}
		// 	ImPlot::PlotLine("Rayleigh", heights.data(), rayleigh_data.data(), n_datapoints);
		// 	ImPlot::PlotLine("Mie", heights.data(), mie_data.data(), n_datapoints);
		// 	ImPlot::PlotLine("Ozone", heights.data(), ozone_data.data(), n_datapoints);
		// 	ImPlot::EndPlot();
		// }

		ImGui::TreePop();
	}
}
void PhysicalSky::DrawSettingsCelestials()
{
	if (ImGui::TreeNodeEx("Sun", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::ColorEdit3("Sun Color", &settings.sun_color.x, hdr_color_edit_flags);
		ImGui::SliderAngle("Sun Aperture", &settings.sun_aperture_angle, 0.f, 90.f, "%.3f deg");
		ImGui::Combo("Limb Darkening Model", &settings.limb_darken_model, "Disabled\0Neckel (Simple)\0Hestroffer (Accurate)\0");
		ImGui::SliderFloat("Limb Darken Strength", &settings.limb_darken_power, 0.f, 5.f, "%.1f");

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Masser", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderAngle("Masser Aperture", &settings.masser_aperture_angle, 0.f, 90.f, "%.3f deg");
		ImGui::SliderFloat("Masser Brightness", &settings.masser_brightness, 0.f, 5.f, "%.1f");

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Secunda", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderAngle("Secunda Aperture", &settings.secunda_aperture_angle, 0.f, 90.f, "%.3f deg");
		ImGui::SliderFloat("Secunda Brightness", &settings.secunda_brightness, 0.f, 5.f, "%.1f");

		ImGui::TreePop();
	}
}

// void drawTrans(const char* label, RE::NiTransform t)
// {
// 	auto rel_pos = t.translate - RE::PlayerCamera::GetSingleton()->pos;
// 	rel_pos.Unitize();
// 	ImGui::InputFloat3(label, &rel_pos.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
// }

void PhysicalSky::DrawSettingsDebug()
{
	ImGui::InputFloat("Timer", &phys_sky_sb_content.timer, 0, 0, "%.6f", ImGuiInputTextFlags_ReadOnly);
	ImGui::InputFloat3("Sun Direction", &phys_sky_sb_content.sun_dir.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
	ImGui::InputFloat3("Masser Direction", &phys_sky_sb_content.masser_dir.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
	ImGui::InputFloat3("Masser Up", &phys_sky_sb_content.masser_upvec.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
	ImGui::InputFloat3("Secunda Direction", &phys_sky_sb_content.secunda_dir.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
	ImGui::InputFloat3("Secunda Up", &phys_sky_sb_content.secunda_upvec.x, "%.3f", ImGuiInputTextFlags_ReadOnly);
	ImGui::InputFloat3("Cam Pos", &phys_sky_sb_content.player_cam_pos.x, "%.3f", ImGuiInputTextFlags_ReadOnly);

	ImGui::BulletText("Transmittance LUT");
	ImGui::Image((void*)(transmittance_lut->srv.get()), { s_transmittance_width, s_transmittance_height });

	ImGui::BulletText("Multiscatter LUT");
	ImGui::Image((void*)(multiscatter_lut->srv.get()), { s_multiscatter_width, s_multiscatter_height });

	ImGui::BulletText("Sky-View LUT");
	ImGui::Image((void*)(sky_view_lut->srv.get()), { s_sky_view_width, s_sky_view_height });

	ImGui::InputScalar("padD4", ImGuiDataType_U32, &RE::Sky::GetSingleton()->masser->padD4);
}
#pragma region IMGUI

void PhysicalSky::Load(json& o_json)
{
	if (o_json[GetName()].is_object())
		settings = o_json[GetName()];

	Feature::Load(o_json);
}

void PhysicalSky::Save(json& o_json)
{
	o_json[GetName()] = settings;
}