#include "SnowSparkles.h"

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
	inline bool isNewFrame() { return isNewFrame(RE::BSGraphics::State::GetSingleton()->uiFrameCount); }
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
	SnowSparkles::Settings,
	screen_space_scale,
	log_microfacet_density,
	microfacet_roughness,
	density_randomization)

void SnowSparkles::DrawSettings()
{
	if (ImGui::TreeNodeEx("Noise", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::SliderFloat("Minimal Glint Size (px)", &settings.screen_space_scale, 1.1, 10.0, "%.1f");
		ImGui::SliderFloat("Log Microfacet Density", &settings.log_microfacet_density, 1.0, 50.0, "%.1f");
		ImGui::SliderFloat("Microfacet Roughness", &settings.microfacet_roughness, 0.005, 0.250);
		ImGui::SliderFloat("Density Randomization", &settings.density_randomization, 0.0, 5.0, "%.1f");

		ImGui::TreePop();
	}

	if (ImGui::TreeNodeEx("Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Image(noiseTexture->srv.get(), { (float)noiseTexture->desc.Width, (float)noiseTexture->desc.Height });
		ImGui::TreePop();
	}
}

void SnowSparkles::Draw(const RE::BSShader* shader, const uint32_t descriptor)
{
	if (!loaded)
		return;
	;

	switch (shader->shaderType.get()) {
	case RE::BSShader::Type::Lighting:
		ModifyLighting(shader, descriptor);
		break;
	}
}

void SnowSparkles::ModifyLighting(const RE::BSShader*, const uint32_t)
{
	auto context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;

	static FrameChecker frame_checker;
	if (frame_checker.isNewFrame()) {
		// update sb
		GlintParameters glint_params = {
			._Glint2023NoiseMapSize = c_noise_tex_size,
			._ScreenSpaceScale = settings.screen_space_scale,
			._LogMicrofacetDensity = settings.log_microfacet_density,
			._MicrofacetRoughness = settings.microfacet_roughness,
			._DensityRandomization = settings.density_randomization
		};

		D3D11_MAPPED_SUBRESOURCE mapped;
		DX::ThrowIfFailed(context->Map(glintSB->resource.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
		memcpy_s(mapped.pData, sizeof(GlintParameters), &glint_params, sizeof(GlintParameters));
		context->Unmap(glintSB->resource.get(), 0);
	}

	ID3D11ShaderResourceView* srv[2] = { noiseTexture->srv.get(), glintSB->srv.get() };
	context->PSSetShaderResources(28, ARRAYSIZE(srv), srv);
}

void SnowSparkles::Load(json& o_json)
{
	if (o_json[GetName()].is_object())
		settings = o_json[GetName()];

	Feature::Load(o_json);
}

void SnowSparkles::Save(json& o_json)
{
	o_json[GetName()] = settings;
}

void SnowSparkles::SetupResources()
{
	logger::debug("Creating noise texture...");
	{
		D3D11_TEXTURE2D_DESC tex_desc{
			.Width = c_noise_tex_size,
			.Height = c_noise_tex_size,
			.MipLevels = 1,
			.ArraySize = 1,
			.Format = DXGI_FORMAT_R32G32B32A32_FLOAT,
			.SampleDesc = { .Count = 1, .Quality = 0 },
			.Usage = D3D11_USAGE_DEFAULT,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS,
			.CPUAccessFlags = 0,
			.MiscFlags = 0
		};
		D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {
			.Format = tex_desc.Format,
			.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
			.Texture2D = {
				.MostDetailedMip = 0,
				.MipLevels = 1 }
		};
		D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {
			.Format = tex_desc.Format,
			.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
			.Texture2D = { .MipSlice = 0 }
		};

		noiseTexture = new Texture2D(tex_desc);
		noiseTexture->CreateSRV(srv_desc);
		noiseTexture->CreateUAV(uav_desc);
	}

	logger::debug("Creating structured buffer...");
	{
		D3D11_BUFFER_DESC sb_desc{
			.ByteWidth = sizeof(GlintParameters),
			.Usage = D3D11_USAGE_DYNAMIC,
			.BindFlags = D3D11_BIND_SHADER_RESOURCE,
			.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
			.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
			.StructureByteStride = sizeof(GlintParameters)
		};
		D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc{
			.Format = DXGI_FORMAT_UNKNOWN,
			.ViewDimension = D3D11_SRV_DIMENSION_BUFFER,
			.Buffer = { .FirstElement = 0, .NumElements = 1 }
		};

		glintSB = std::make_unique<Buffer>(sb_desc);
		glintSB->CreateSRV(srv_desc);
	}

	CompileComputeShaders();
}

void SnowSparkles::CompileComputeShaders()
{
	logger::debug("Compiling noiseGenProgram...");
	auto noiseGenProgramPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(L"Data\\Shaders\\SnowSparkles\\noisegen.cs.hlsl", {}, "cs_5_0"));
	if (noiseGenProgramPtr)
		noiseGenProgram.attach(noiseGenProgramPtr);

	GenerateNoise();
}

void SnowSparkles::GenerateNoise()
{
	auto context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;

	struct OldState
	{
		ID3D11ComputeShader* shader;
		ID3D11UnorderedAccessView* uav[1];
		ID3D11ClassInstance* instance;
		UINT numInstances;
	};

	OldState newer{}, old{};
	context->CSGetShader(&old.shader, &old.instance, &old.numInstances);
	context->CSGetUnorderedAccessViews(0, ARRAYSIZE(old.uav), old.uav);

	logger::debug("Generating noise...");
	{
		newer.uav[0] = noiseTexture->uav.get();
		context->CSSetShader(noiseGenProgram.get(), nullptr, 0);
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(newer.uav), newer.uav, nullptr);
		context->Dispatch(((c_noise_tex_size - 1) >> 5) + 1, ((c_noise_tex_size - 1) >> 5) + 1, 1);
	}

	context->CSSetShader(old.shader, &old.instance, old.numInstances);
	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(old.uav), old.uav, nullptr);
}

void SnowSparkles::ClearShaderCache()
{
	if (noiseGenProgram) {
		noiseGenProgram->Release();
		noiseGenProgram = nullptr;
	}
	CompileComputeShaders();
}