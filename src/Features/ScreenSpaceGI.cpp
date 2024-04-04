#include "ScreenSpaceGI.h"

#include "Bindings.h"
#include "State.h"
#include "Util.h"

void ScreenSpaceGI::Load(json& o_json)
{
	// if (o_json[GetName()].is_object())
	// 	settings = o_json[GetName()];

	Feature::Load(o_json);
}

void ScreenSpaceGI::Save(json&)
{
	// o_json[GetName()] = settings;
}

void ScreenSpaceGI::RestoreDefaultSettings()
{
	settings = {};
}

void ScreenSpaceGI::DrawSettings()
{
	ImGui::Checkbox("Enabled", &settings.Enabled);
	ImGui::SliderFloat("Depth Rejection", &settings.DepthRejection, .1f, 100.f, "%.1f");
	ImGui::SliderFloat("Depth Threshold", &settings.DepthThreshold, 0.f, 100.f, "%.2f");
	ImGui::SliderFloat("Normal Threshold", &settings.NormalThreshold, .1f, 100.f, "%.1f");
}

////////////////////////////////////////////////////////////////////////////////

void ScreenSpaceGI::CompileComputeShaders()
{
	struct ShaderCompileInfo
	{
		winrt::com_ptr<ID3D11ComputeShader>* programPtr;
		std::string_view filename;
		std::vector<std::pair<const char*, const char*>> defines;
	};

	std::vector<ShaderCompileInfo>
		shaderInfos = {
			{ &deinterleaveAtlasCS, "ssgi_deinterleaveCS.hlsl", { { "ATLAS", "" } } },
			{ &deinterleaveRegularCS, "ssgi_deinterleaveCS.hlsl", {} },
			{ &giCS, "ssgiCS.hlsl", {} },
			{ &giWideCS, "ssgiCS.hlsl", { { "WIDE", "" } } },
			{ &upsampleCS, "ssgi_upsampleCS.hlsl", {} },
			// { &temporalFilterCompute, "temporal.cs.hlsl", {} }
		};

	for (auto& info : shaderInfos) {
		try {
			auto path = std::filesystem::path("Data\\Shaders\\ScreenSpaceGI") / info.filename;
			if (auto rawPtr = reinterpret_cast<ID3D11ComputeShader*>(Util::CompileShader(path.c_str(), info.defines, "cs_5_0")))
				info.programPtr->attach(rawPtr);
		} catch (std::exception e) {
			logger::error("error compiling {}: {}", info.filename, e.what());
			continue;
		}
	}
}

void ScreenSpaceGI::SetupResources()
{
	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	auto device = renderer->GetRuntimeData().forwarder;

	logger::debug("Creating samplers...");
	{
		D3D11_SAMPLER_DESC samplerDesc = {};
		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
		samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		samplerDesc.MaxAnisotropy = 1;
		samplerDesc.MinLOD = 0;
		samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, samplerPointClamp.put()));

		samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;

		DX::ThrowIfFailed(device->CreateSamplerState(&samplerDesc, samplerLinearClamp.put()));
	}

	logger::debug("Creating buffers...");
	{
		ssgiCB = eastl::make_unique<ConstantBuffer>(ConstantBufferDesc<SSGICB>());
	}

	logger::debug("Creating textures...");
	{
		auto mainTex = renderer->GetRuntimeData().renderTargets[RE::RENDER_TARGETS::kMAIN];
		D3D11_TEXTURE2D_DESC texDesc{};
		mainTex.texture->GetDesc(&texDesc);

		texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;

		{
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
				.Format = texDesc.Format,
				.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
				.Texture2D = {
					.MostDetailedMip = 0,
					.MipLevels = 1 }
			};
			D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
				.Format = texDesc.Format,
				.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
				.Texture2D = { .MipSlice = 0 }
			};

			texOutput = eastl::make_unique<Texture2D>(texDesc);
			texOutput->CreateSRV(srvDesc);
			texOutput->CreateUAV(uavDesc);
		}

		uint resolution[2] = { ((texDesc.Width + 63) >> 6) << 6, ((texDesc.Height + 63) >> 6) << 6 };

		texDesc.Width = (resolution[0] + 7) >> 3;
		texDesc.Height = (resolution[1] + 7) >> 3;
		texDesc.Format = DXGI_FORMAT_R32_FLOAT;
		texDesc.MipLevels = 4;
		texDesc.ArraySize = 16;

		{
			texArrayAtlasDepth = eastl::make_unique<Texture2D>(texDesc);

			for (uint i = 0; i < uavsArrayAtlasDepth.size(); ++i) {
				D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
					.Format = texDesc.Format,
					.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY,
					.Texture2DArray = {
						.MostDetailedMip = i,
						.MipLevels = 1,
						.FirstArraySlice = 0,
						.ArraySize = texDesc.ArraySize }
				};
				DX::ThrowIfFailed(device->CreateShaderResourceView(texArrayAtlasDepth->resource.get(), &srvDesc, srvsArrayAtlasDepth[i].put()));

				D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
					.Format = texDesc.Format,
					.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY,
					.Texture2DArray = { .MipSlice = i, .ArraySize = texDesc.ArraySize }
				};
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texArrayAtlasDepth->resource.get(), &uavDesc, uavsArrayAtlasDepth[i].put()));
			}
		}

		texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;

		{
			texArrayAtlasColor = eastl::make_unique<Texture2D>(texDesc);
			for (uint i = 0; i < uavsArrayAtlasColor.size(); ++i) {
				D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
					.Format = texDesc.Format,
					.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY,
					.Texture2DArray = {
						.MostDetailedMip = i,
						.MipLevels = 1,
						.FirstArraySlice = 0,
						.ArraySize = texDesc.ArraySize }
				};
				DX::ThrowIfFailed(device->CreateShaderResourceView(texArrayAtlasColor->resource.get(), &srvDesc, srvsArrayAtlasColor[i].put()));

				D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
					.Format = texDesc.Format,
					.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY,
					.Texture2DArray = { .MipSlice = i, .ArraySize = texDesc.ArraySize }
				};
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texArrayAtlasColor->resource.get(), &uavDesc, uavsArrayAtlasColor[i].put()));
			}
		}

		texDesc.Width = (resolution[0] + 1) >> 1;
		texDesc.Height = (resolution[1] + 1) >> 1;
		texDesc.ArraySize = 1;
		texDesc.Format = DXGI_FORMAT_R32_FLOAT;

		{
			texDepth = eastl::make_unique<Texture2D>(texDesc);
			for (uint i = 0; i < uavsDepth.size(); ++i) {
				D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
					.Format = texDesc.Format,
					.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
					.Texture2D = {
						.MostDetailedMip = i,
						.MipLevels = 1 }
				};
				DX::ThrowIfFailed(device->CreateShaderResourceView(texDepth->resource.get(), &srvDesc, srvsDepth[i].put()));

				D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
					.Format = texDesc.Format,
					.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
					.Texture2D = { .MipSlice = i }
				};
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texDepth->resource.get(), &uavDesc, uavsDepth[i].put()));
			}
		}

		texDesc.Format = DXGI_FORMAT_R16G16_FLOAT;

		{
			texNormal = eastl::make_unique<Texture2D>(texDesc);
			for (uint i = 0; i < uavsNormal.size(); ++i) {
				D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
					.Format = texDesc.Format,
					.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
					.Texture2D = {
						.MostDetailedMip = i,
						.MipLevels = 1 }
				};
				DX::ThrowIfFailed(device->CreateShaderResourceView(texNormal->resource.get(), &srvDesc, srvsNormal[i].put()));

				D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
					.Format = texDesc.Format,
					.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
					.Texture2D = { .MipSlice = i }
				};
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texNormal->resource.get(), &uavDesc, uavsNormal[i].put()));
			}
		}

		texDesc.Format = DXGI_FORMAT_R11G11B10_FLOAT;

		{
			texDiffuse = eastl::make_unique<Texture2D>(texDesc);
			for (uint i = 0; i < uavsDiffuse.size(); ++i) {
				D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {
					.Format = texDesc.Format,
					.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
					.Texture2D = {
						.MostDetailedMip = i,
						.MipLevels = 1 }
				};
				DX::ThrowIfFailed(device->CreateShaderResourceView(texDiffuse->resource.get(), &srvDesc, srvsDiffuse[i].put()));

				D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
					.Format = texDesc.Format,
					.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
					.Texture2D = { .MipSlice = i }
				};
				DX::ThrowIfFailed(device->CreateUnorderedAccessView(texDiffuse->resource.get(), &uavDesc, uavsDiffuse[i].put()));
			}
		}
	}

	CompileComputeShaders();
}

void ScreenSpaceGI::ClearShaderCache()
{
	static const std::vector<winrt::com_ptr<ID3D11ComputeShader>*> shaderPtrs = {
		&deinterleaveAtlasCS, &deinterleaveRegularCS, &giCS, &giWideCS, &upsampleCS
	};

	for (auto shader : shaderPtrs)
		if ((*shader)) {
			(*shader)->Release();
			shader->detach();
		}

	CompileComputeShaders();
}

ScreenSpaceGI::SSGICB ScreenSpaceGI::GetBaseCBData()
{
	auto viewport = RE::BSGraphics::State::GetSingleton();
	auto state = RE::BSGraphics::RendererShadowState::GetSingleton();
	auto eye = (!REL::Module::IsVR()) ? state->GetRuntimeData().cameraData.getEye() :
	                                    state->GetVRRuntimeData().cameraData.getEye();
	auto projMat = (!REL::Module::IsVR()) ? state->GetRuntimeData().cameraData.getEye().projMat :
	                                        state->GetVRRuntimeData().cameraData.getEye().projMat;
	float2 dynRes = float2{ State::GetSingleton()->screenWidth, State::GetSingleton()->screenHeight } * viewport->GetRuntimeData().dynamicResolutionCurrentWidthScale;
	dynRes = { floor(dynRes.x * .5f), floor(dynRes.y * .5f) };

	SSGICB data = {
		.DepthRejection = 1.f / settings.DepthRejection,
		.DepthThreshold = settings.DepthThreshold,
		.NormalThreshold = settings.NormalThreshold,
		.NDCToViewMul = { 2.0f / eye.projMat(0, 0), -2.0f / eye.projMat(1, 1) },
		.NDCToViewAdd = { -1.0f / eye.projMat(0, 0), 1.0f / eye.projMat(1, 1) },
		.DepthUnpackConsts = { -eye.projMat(3, 2), eye.projMat(2, 2) },
	};

	return data;
}

void ScreenSpaceGI::DrawGI()
{
	FLOAT clr[4] = { 0, 0, 0, 1 };
	auto context = RE::BSGraphics::Renderer::GetSingleton()->GetRuntimeData().context;

	context->ClearUnorderedAccessViewFloat(texOutput->uav.get(), clr);

	if (!(settings.Enabled && ShadersOK()))
		return;

	for (int i = 0; i < uavsArrayAtlasDepth.size(); ++i) {
		context->ClearUnorderedAccessViewFloat(uavsArrayAtlasDepth[i].get(), clr);
		context->ClearUnorderedAccessViewFloat(uavsArrayAtlasColor[i].get(), clr);
		context->ClearUnorderedAccessViewFloat(uavsDepth[i].get(), clr);
		context->ClearUnorderedAccessViewFloat(uavsNormal[i].get(), clr);
		context->ClearUnorderedAccessViewFloat(uavsDiffuse[i].get(), clr);
	}
	//////////////////////////////////////////////////////

	auto cbdata = GetBaseCBData();

	//////////////////////////////////////////////////////

	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	auto rts = renderer->GetRuntimeData().renderTargets;
	auto bindings = Bindings::GetSingleton();

	ID3D11ShaderResourceView* srvs[5] = { nullptr };
	ID3D11UnorderedAccessView* uavs[8] = { nullptr };
	ID3D11SamplerState* samplers[2] = { samplerLinearClamp.get(), samplerPointClamp.get() };

	auto resetViews = [&]() {
		memset(srvs, 0, sizeof(void*) * ARRAYSIZE(srvs));
		memset(uavs, 0, sizeof(void*) * ARRAYSIZE(uavs));

		context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);
		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
	};

	//////////////////////////////////////////////////////

	auto cb = ssgiCB->CB();
	context->CSSetConstantBuffers(1, 1, &cb);
	context->CSSetSamplers(0, ARRAYSIZE(samplers), samplers);

	// deinterleaving
	{
		srvs[0] = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY].depthSRV;
		srvs[1] = rts[NORMALROUGHNESS].SRV;
		srvs[2] = rts[bindings->forwardRenderTargets[0]].SRV;
		srvs[3] = rts[RE::RENDER_TARGET::kMOTION_VECTOR].SRV;

		for (int i = 0; i < uavsArrayAtlasDepth.size(); ++i)
			uavs[i] = uavsArrayAtlasDepth[i].get();
		for (int i = 0; i < uavsArrayAtlasColor.size(); ++i)
			uavs[i + 4] = uavsArrayAtlasColor[i].get();

		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
		context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);
		context->CSSetShader(deinterleaveAtlasCS.get(), nullptr, 0);
		context->Dispatch(texArrayAtlasDepth->desc.Width >> 1, texArrayAtlasDepth->desc.Height >> 1, 1);

		for (int i = 0; i < uavsDepth.size(); ++i)
			uavs[i] = uavsDepth[i].get();
		for (int i = 0; i < uavsNormal.size(); ++i)
			uavs[i + 4] = uavsNormal[i].get();

		context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
		context->CSSetShader(deinterleaveRegularCS.get(), nullptr, 0);
		context->Dispatch(texArrayAtlasDepth->desc.Width >> 1, texArrayAtlasDepth->desc.Height >> 1, 1);
	}

	// sampling
	{
		int ranges[4] = { 2, 2, 4, 8 };
		float spreads[4] = { 2, 2, 4, 2 };
		int dispatchDiv[4] = { 8, 8, 16, 16 };
		ID3D11ComputeShader* shaders[4] = { giCS.get(), giCS.get(), giWideCS.get(), giWideCS.get() };
		for (int i = (int)uavsDepth.size() - 1; i >= 0; --i) {
			resetViews();

			cbdata.BufferDim = { texArrayAtlasDepth->desc.Width >> i, texArrayAtlasDepth->desc.Height >> i };
			cbdata.RcpBufferDim = { 1.f / cbdata.BufferDim.x, 1.f / cbdata.BufferDim.y };
			cbdata.Range = ranges[i];
			cbdata.Spread = spreads[i];
			cbdata.RcpRangeSpreadSqr = 1.f / (cbdata.Range * cbdata.Spread);
			cbdata.RcpRangeSpreadSqr = cbdata.RcpRangeSpreadSqr * cbdata.RcpRangeSpreadSqr;
			ssgiCB->Update(cbdata);

			srvs[0] = srvsArrayAtlasDepth[i].get();
			srvs[1] = srvsArrayAtlasColor[i].get();
			srvs[2] = srvsNormal[i].get();

			uavs[0] = uavsDiffuse[i].get();

			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
			context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);
			context->CSSetShader(shaders[i], nullptr, 0);
			context->Dispatch(
				((uint)cbdata.BufferDim.x + dispatchDiv[i] - 1) / dispatchDiv[i],
				((uint)cbdata.BufferDim.y + dispatchDiv[i] - 1) / dispatchDiv[i],
				16);
		}
	}

	// upsample
	{
		context->CSSetShader(upsampleCS.get(), nullptr, 0);

		int ranges[4] = { 1, 1, 2, 2 };
		float spreads[4] = { 8, 8, 8, 4 };
		for (int i = (int)uavsDepth.size() - 1; i >= 0; --i) {
			resetViews();

			if (i == 0) {
				cbdata.BufferDim = { ((texOutput->desc.Width + 63) >> 6) << 6, ((texOutput->desc.Height + 63) >> 6) << 6 };
			} else
				cbdata.BufferDim = { texDiffuse->desc.Width >> (i - 1), texDiffuse->desc.Height >> (i - 1) };
			cbdata.RcpBufferDim = { 1.f / cbdata.BufferDim.x, 1.f / cbdata.BufferDim.y };
			cbdata.Range = ranges[i];
			cbdata.Spread = spreads[i];
			cbdata.RcpRangeSpreadSqr = 1.f / (cbdata.Range * cbdata.Spread);
			cbdata.RcpRangeSpreadSqr = cbdata.RcpRangeSpreadSqr * cbdata.RcpRangeSpreadSqr;
			ssgiCB->Update(cbdata);

			srvs[0] = srvsDepth[i].get();
			srvs[1] = srvsNormal[i].get();
			srvs[2] = srvsDiffuse[i].get();
			if (i == 0) {
				srvs[3] = renderer->GetDepthStencilData().depthStencils[RE::RENDER_TARGETS_DEPTHSTENCIL::kPOST_ZPREPASS_COPY].depthSRV;
				srvs[4] = rts[NORMALROUGHNESS].SRV;

				uavs[0] = texOutput->uav.get();
			} else {
				srvs[3] = srvsDepth[i - 1].get();
				srvs[4] = srvsNormal[i - 1].get();

				uavs[0] = uavsDiffuse[i - 1].get();
			}

			context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
			context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);
			if (i == 0)
				context->Dispatch(
					(texOutput->desc.Width + 7) / 8,
					(texOutput->desc.Height + 7) / 8,
					1);
			else
				context->Dispatch(
					((uint)cbdata.BufferDim.x + 7) / 8,
					((uint)cbdata.BufferDim.y + 7) / 8,
					1);
		}
	}

	// cleanup
	memset(srvs, 0, sizeof(void*) * ARRAYSIZE(srvs));
	memset(uavs, 0, sizeof(void*) * ARRAYSIZE(uavs));
	memset(samplers, 0, sizeof(void*) * ARRAYSIZE(samplers));

	context->CSSetUnorderedAccessViews(0, ARRAYSIZE(uavs), uavs, nullptr);
	context->CSSetShaderResources(0, ARRAYSIZE(srvs), srvs);
	context->CSSetSamplers(0, ARRAYSIZE(samplers), samplers);
	context->CSSetShader(nullptr, nullptr, 0);
}
