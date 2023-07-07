#include "PhysicalSky.h"

#include <implot.h>

#include "Util.h"

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

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PhysicalSky::UserContols,
	enable_sky,
	enable_scatter,
	transmittance_step,
	multiscatter_step,
	multiscatter_sqrt_samples,
	skyview_step,
	aerial_perspective_max_dist)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PhysicalSky::WorldspaceControls,
	enable_sky,
	enable_scatter,
	unit_scale,
	bottom_z,
	ground_radius,
	atmos_thickness)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PhysicalSky::WeatherControls,
	ground_albedo,
	source_illuminance,
	limb_darken_model,
	sun_luminance,
	sun_half_angle,
	rayleigh_scatter,
	rayleigh_absorption,
	rayleigh_decay,
	mie_scatter,
	mie_absorption,
	mie_decay,
	ozone_absorption,
	ozone_height,
	ozone_thickness)
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PhysicalSky::Settings,
	user,
	use_debug_worldspace,
	debug_worldspace,
	use_debug_weather,
	debug_weather)

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

void PhysicalSky::Draw(const RE::BSShader* shader, [[maybe_unused]] const uint32_t descriptor)
{
	if (!loaded)
		return;

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

	UpdatePhysSkySB();

	switch (shader->shaderType.get()) {
	case RE::BSShader::Type::Sky:
		if (phys_sky_sb_content.enable_sky)
			GenerateLuts();
		if (descriptor == std::underlying_type_t<SkyShaderTechniques>(SkyShaderTechniques::Sky))
			ModifySky();
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

	bool enable_sky =
		settings.user.enable_sky &&
		settings.debug_worldspace.enable_sky &&
		(RE::Sky::GetSingleton()->mode.get() == RE::Sky::Mode::kFull);

	auto accumulator = RE::BSGraphics::BSShaderAccumulator::GetCurrentAccumulator();
	auto dir_light = skyrim_cast<RE::NiDirectionalLight*>(accumulator->GetRuntimeData().activeShadowSceneNode->GetRuntimeData().sunLight->light.get());
	if (!dir_light)
		return;
	auto sun_dir = dir_light->GetWorldDirection();

	auto cam_pos = RE::PlayerCamera::GetSingleton()->pos;

	phys_sky_sb_content = {
		.sun_dir = { -sun_dir.x, -sun_dir.y, -sun_dir.z },
		.player_cam_pos = { cam_pos.x, cam_pos.y, cam_pos.z },

		.enable_sky = enable_sky,
		.enable_scatter = settings.user.enable_scatter && settings.debug_worldspace.enable_scatter,

		.transmittance_step = settings.user.transmittance_step,
		.multiscatter_step = settings.user.multiscatter_step,
		.multiscatter_sqrt_samples = settings.user.multiscatter_sqrt_samples,
		.skyview_step = settings.user.skyview_step,
		.aerial_perspective_max_dist = settings.user.aerial_perspective_max_dist,

		.unit_scale = settings.debug_worldspace.unit_scale,
		.bottom_z = settings.debug_worldspace.bottom_z,
		.ground_radius = settings.debug_worldspace.ground_radius,
		.atmos_thickness = settings.debug_worldspace.atmos_thickness,

		.ground_albedo = settings.debug_weather.ground_albedo,

		.source_illuminance = settings.debug_weather.source_illuminance,

		.limb_darken_model = settings.debug_weather.limb_darken_model,
		.sun_luminance = settings.debug_weather.sun_luminance,
		.sun_half_angle = settings.debug_weather.sun_half_angle,

		.rayleigh_scatter = settings.debug_weather.rayleigh_scatter,
		.rayleigh_absorption = settings.debug_weather.rayleigh_absorption,
		.rayleigh_decay = settings.debug_weather.rayleigh_decay,

		.mie_scatter = settings.debug_weather.mie_scatter,
		.mie_absorption = settings.debug_weather.mie_absorption,
		.mie_decay = settings.debug_weather.mie_decay,

		.ozone_absorption = settings.debug_weather.ozone_absorption,
		.ozone_height = settings.debug_weather.ozone_height,
		.ozone_thickness = settings.debug_weather.ozone_thickness,

		.ap_inscatter_mix = settings.debug_weather.ap_inscatter_mix,
		.ap_transmittance_mix = settings.debug_weather.ap_transmittance_mix,
		.light_transmittance_mix = settings.debug_weather.light_transmittance_mix
	};

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

void PhysicalSky::ModifySky()
{
	auto context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;

	context->PSSetShaderResources(16, 1, sky_view_lut->srv.put());
	context->PSSetShaderResources(17, 1, transmittance_lut->srv.put());
	context->PSSetShaderResources(18, 1, phys_sky_sb->srv.put());
}

void PhysicalSky::DrawSettings()
{
	static int pagenum = 0;

	ImGui::Combo("Page", &pagenum, "General\0Worldspace\0Weather\0Debug\0");

	ImGui::Separator();

	switch (pagenum) {
	case 0:
		DrawSettingsUser();
		break;
	case 1:
		DrawSettingsWorldspace();
		break;
	case 2:
		DrawSettingsWeather();
		break;
	case 3:
		DrawSettingsDebug();
		break;
	default:
		break;
	}
}

void PhysicalSky::DrawSettingsUser()
{
	ImGui::Checkbox("Enable Physcial Sky", reinterpret_cast<bool*>(&settings.user.enable_sky));

	ImGui::Indent();
	{
		ImGui::Checkbox("Enable Aerial Perspective", reinterpret_cast<bool*>(&settings.user.enable_scatter));
	}
	ImGui::Unindent();

	if (ImGui::TreeNodeEx("Quality", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Text("The bigger the settings below, the more accurate and more performance-heavy things are.");
		ImGui::DragScalar("Transmittance Steps", ImGuiDataType_U32, &settings.user.transmittance_step);
		ImGui::DragScalar("Multiscatter Steps", ImGuiDataType_U32, &settings.user.multiscatter_step);
		ImGui::DragScalar("Multiscatter Sqrt Samples", ImGuiDataType_U32, &settings.user.multiscatter_sqrt_samples);
		ImGui::DragScalar("Sky View Steps", ImGuiDataType_U32, &settings.user.skyview_step);
		ImGui::DragFloat("Aerial Perspective Max Dist", &settings.user.aerial_perspective_max_dist);
		ImGui::TreePop();
	}
}

void PhysicalSky::DrawSettingsWorldspace()
{
	ImGui::SliderFloat2("Unit Scale", &settings.debug_worldspace.unit_scale.x, 0.1f, 50.f);
}

void PhysicalSky::DrawSettingsWeather()
{
	constexpr auto hdr_color_edit_flags = ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR;

	ImGui::Checkbox("Override Weather", reinterpret_cast<bool*>(&settings.use_debug_weather));

	if (!settings.use_debug_weather)
		ImGui::BeginDisabled();
	{
		ImGui::ColorEdit3("Ground Albedo", &settings.debug_weather.ground_albedo.x, hdr_color_edit_flags);

		ImGui::ColorEdit3("Source Illuminance", &settings.debug_weather.source_illuminance.x, hdr_color_edit_flags);
		ImGui::ColorEdit3("Sun Luminance", &settings.debug_weather.sun_luminance.x, hdr_color_edit_flags);
		ImGui::SliderAngle("Sun Half Angle", &settings.debug_weather.sun_half_angle, 0.f, 90.f, "%.3f deg");

		if (ImGui::TreeNodeEx("Participating Media", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::TextWrapped(
				"Rayleigh scatter: scattering by particles smaller than the wavelength of light, like air molecules, "
				"generally isotropic. This what makes the sky blue, and red at sunset. Usually needs no change.");
			ImGui::ColorEdit3("Rayleigh Scatter", &settings.debug_weather.rayleigh_scatter.x, hdr_color_edit_flags);
			ImGui::ColorEdit3("Rayleigh Absorption", &settings.debug_weather.rayleigh_absorption.x, hdr_color_edit_flags);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Usually zero");
			ImGui::DragFloat("Rayleigh Height Decay", &settings.debug_weather.rayleigh_decay, .1f, 0.f, 100.f);

			ImGui::TextWrapped(
				"Mie scatter: scattering by particles similar to the wavelength of light, like dust particles, "
				"with strong forward scattering characteristics. This generates glow around the sun. Increase in dustier weather.");
			ImGui::ColorEdit3("Mie Scatter", &settings.debug_weather.mie_scatter.x, hdr_color_edit_flags);
			ImGui::ColorEdit3("Mie Absorption", &settings.debug_weather.mie_absorption.x, hdr_color_edit_flags);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("Usually 1/9 of scatter coefficient. Dust/pollution is lower, fog is higher");
			ImGui::DragFloat("Mie Height Decay", &settings.debug_weather.mie_decay, .1f, 0.f, 100.f);

			ImGui::TextWrapped(
				"Ozone absorption: light absorption by the high ozone layer. This keeps the zenith sky blue, especially at sunrise or sunset.");
			ImGui::ColorEdit3("Ozone Absorption", &settings.debug_weather.ozone_absorption.x, hdr_color_edit_flags);
			ImGui::DragFloat("Ozone Mean Height", &settings.debug_weather.ozone_height, .1f, 0.f, 100.f);
			ImGui::DragFloat("Ozone Layer Thickness", &settings.debug_weather.ozone_thickness, .1f, 0.f, 50.f);

			if (ImPlot::BeginPlot("Media Density", { -1, 0 }, ImPlotFlags_NoInputs)) {
				ImPlot::SetupAxis(ImAxis_X1, "Altitude / km");
				ImPlot::SetupAxis(ImAxis_Y1, "Relative Density");
				ImPlot::SetupLegend(ImPlotLocation_NorthEast);
				ImPlot::SetupFinish();

				constexpr size_t n_datapoints = 101;
				std::array<float, n_datapoints> heights, rayleigh_data, mie_data, ozone_data;
				for (size_t i = 0; i < n_datapoints; i++) {
					heights[i] = phys_sky_sb_content.atmos_thickness * 1e3f * i / (n_datapoints - 1);
					rayleigh_data[i] = exp(-heights[i] / settings.debug_weather.rayleigh_decay);
					mie_data[i] = exp(-heights[i] / settings.debug_weather.mie_decay);
					ozone_data[i] = max(0.f, 1 - abs(heights[i] - settings.debug_weather.ozone_height) / (settings.debug_weather.ozone_thickness * .5f));
				}
				ImPlot::PlotLine("Rayleigh", heights.data(), rayleigh_data.data(), n_datapoints);
				ImPlot::PlotLine("Mie", heights.data(), mie_data.data(), n_datapoints);
				ImPlot::PlotLine("Ozone", heights.data(), ozone_data.data(), n_datapoints);
				ImPlot::EndPlot();
			}

			ImGui::TreePop();
		}

		ImGui::SliderFloat("In-scatter Mix", &settings.debug_weather.ap_inscatter_mix, 0.f, 1.f);
		ImGui::SliderFloat("View Transmittance Mix", &settings.debug_weather.ap_transmittance_mix, 0.f, 1.f);
		ImGui::SliderFloat("Light Transmittance Mix", &settings.debug_weather.light_transmittance_mix, 0.f, 1.f);
	}
	if (!settings.use_debug_weather)
		ImGui::EndDisabled();
}

void PhysicalSky::DrawSettingsDebug()
{
	ImGui::InputFloat3("Sun Direction", &phys_sky_sb_content.sun_dir.x, "%.3f", ImGuiInputTextFlags_ReadOnly);

	ImGui::BulletText("Transmittance LUT");
	ImGui::Image((void*)(transmittance_lut->srv.get()), { s_transmittance_width, s_transmittance_height });

	ImGui::BulletText("Multiscatter LUT");
	ImGui::Image((void*)(multiscatter_lut->srv.get()), { s_multiscatter_width, s_multiscatter_height });

	ImGui::BulletText("Sky-View LUT");
	ImGui::Image((void*)(sky_view_lut->srv.get()), { s_sky_view_width, s_sky_view_height });
}

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