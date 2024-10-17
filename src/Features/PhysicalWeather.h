#pragma once

#include "Buffer.h"
#include "Feature.h"

struct PhysicalWeather : Feature
{
	static PhysicalWeather* GetSingleton()
	{
		static PhysicalWeather singleton;
		return &singleton;
	}

	bool inline SupportsVR() override { return false; }

	virtual inline std::string GetName() override { return "Physical Weather"; }
	virtual inline std::string GetShortName() override { return "PhysicalWeather"; }

	virtual inline void RestoreDefaultSettings() override{};
	virtual void DrawSettings() override;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void SetupResources() override;
	virtual void ClearShaderCache() override;
	void CompileComputeShaders();

	virtual void Prepass() override;
	void UpdateCB();
	void GenerateLuts();

	/////////////////////////////////////////////////////////////////////////////////////////

	struct AtmosphereParams
	{
		float rayleigh_decay = 1.f / 8.69645f;
		float3 rayleigh_absorption = { 0.f, 0.f, 0.f };  //
		float3 rayleigh_scatter = { 6.6049f, 12.345f, 29.413f };

		float ozone_altitude = 22.3499f + 35.66071f * .5f;  //
		float ozone_thickness = 35.66071f;
		float3 ozone_absorption = { 2.2911f, 1.5404f, 0 };  //

		float aerosol_decay = 1 / 1.2f;
		float3 aerosol_absorption = { .444f, .444f, .444f };  //
		float3 aerosol_scatter = { 3.996f, 3.996f, 3.996f };

		float _pad;
	};
	static_assert(sizeof(AtmosphereParams) % 16 == 0);

	struct Settings
	{
		float bottom_z = -1.5e4f;  // in game unit

		float unit_scale = 5.f;
		float3 ground_albedo = { .2f, .2f, .2f };
		float planet_radius = 6.36e3f;
		float atmosphere_height = 100.f;

		AtmosphereParams atmosphere;
	} settings;

	struct PhysWeatherCB
	{
		float3 dirlight_dir;
		float cam_height_km;  //

		float3 ground_albedo;
		float _pad;  //

		float unit_scale;
		float planet_radius;
		float atmosphere_height;
		float bottom_z;  //

		AtmosphereParams atmosphere;
	} cb_data;
	static_assert(sizeof(PhysWeatherCB) % 16 == 0);

	eastl::unique_ptr<ConstantBuffer> phys_weather_cb;

	winrt::com_ptr<ID3D11SamplerState> linear_ccc_samp = nullptr;
	winrt::com_ptr<ID3D11SamplerState> linear_wmc_samp = nullptr;

	constexpr static uint16_t s_transmittance_width = 256;
	constexpr static uint16_t s_transmittance_height = 64;
	constexpr static uint16_t s_multiscatter_width = 32;
	constexpr static uint16_t s_multiscatter_height = 32;
	constexpr static uint16_t s_sky_view_width = 200;
	constexpr static uint16_t s_sky_view_height = 150;
	constexpr static uint16_t s_aerial_perspective_width = 32;
	constexpr static uint16_t s_aerial_perspective_height = 32;
	constexpr static uint16_t s_aerial_perspective_depth = 32;

	eastl::unique_ptr<Texture2D> transmittance_lut = nullptr;
	eastl::unique_ptr<Texture2D> multiscatter_lut = nullptr;
	eastl::unique_ptr<Texture2D> sky_view_lut = nullptr;
	eastl::unique_ptr<Texture3D> aerial_perspective_lut = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> transmittance_cs = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> multiscatter_cs = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> sky_view_cs = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> aerial_perspective_cs = nullptr;
};