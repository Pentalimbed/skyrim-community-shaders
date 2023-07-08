#pragma once

#include "Buffer.h"
#include "Feature.h"

struct PhysicalSky : Feature
{
	// boilerplates
	inline static auto* GetSingleton()
	{
		static PhysicalSky singleton;
		return &singleton;
	}

	virtual inline std::string GetName() { return "Physical Sky"; }
	virtual inline std::string GetShortName() { return "PhysicalSky"; }

	// structs
	constexpr static uint16_t s_transmittance_width = 256;
	constexpr static uint16_t s_transmittance_height = 64;
	constexpr static uint16_t s_multiscatter_width = 32;
	constexpr static uint16_t s_multiscatter_height = 32;
	constexpr static uint16_t s_sky_view_width = 200;
	constexpr static uint16_t s_sky_view_height = 150;
	constexpr static uint16_t s_aerial_perspective_width = 32;
	constexpr static uint16_t s_aerial_perspective_height = 32;
	constexpr static uint16_t s_aerial_perspective_depth = 64;

	// io
	struct UserContols
	{
		uint32_t enable_sky = true;
		uint32_t enable_scatter = true;

		uint32_t transmittance_step = 40;
		uint32_t multiscatter_step = 20;
		uint32_t multiscatter_sqrt_samples = 4;
		uint32_t skyview_step = 30;
		float aerial_perspective_max_dist = 50;  // in km
	};

	struct WorldspaceControls
	{
		uint32_t enable_sky = true;
		uint32_t enable_scatter = true;

		DirectX::XMFLOAT2 unit_scale = { 5, 1 };
		float bottom_z = -15000;      // in game unit
		float ground_radius = 6.36f;  // in megameter
		float atmos_thickness = .1f;
	};

	struct WeatherControls
	{
		DirectX::XMFLOAT3 ground_albedo = { .3f, .3f, .3f };

		uint32_t limb_darken_model = 1;
		DirectX::XMFLOAT3 sun_intensity = { 3, 3, 3 };        // 1.69e9 cd m^-2
		float sun_half_angle = 0.545 * 3.1415926535 / 180.0;  // in rad

		DirectX::XMFLOAT3 rayleigh_scatter = { 5.802f, 13.558f, 33.1f };  // in megameter^-1
		DirectX::XMFLOAT3 rayleigh_absorption = { 0.f, 0.f, 0.f };
		float rayleigh_decay = 8.f;  // in km^-1

		DirectX::XMFLOAT3 mie_scatter = { 3.996f, 3.996f, 3.996f };
		DirectX::XMFLOAT3 mie_absorption = { .444f, .444f, .444f };
		float mie_decay = 1.2f;

		DirectX::XMFLOAT3 ozone_absorption = { 0.650f, 1.881f, 0.085f };
		float ozone_height = 25.f;  // in km
		float ozone_thickness = 30.f;

		float ap_inscatter_mix = 1.f;
		float ap_transmittance_mix = 1.f;
		float light_transmittance_mix = 2.f;
	};

	struct Settings
	{
		UserContols user;

		bool use_debug_worldspace = false;
		WorldspaceControls debug_worldspace;

		bool use_debug_weather = false;
		WeatherControls debug_weather;
	} settings;

	virtual void DrawSettings();
	void DrawSettingsUser();
	void DrawSettingsWorldspace();
	void DrawSettingsWeather();
	void DrawSettingsDebug();

	virtual void Load(json& o_json);
	virtual void Save(json& o_json);

	// resources
	winrt::com_ptr<ID3D11SamplerState> common_clamp_sampler = nullptr;

	std::unique_ptr<Texture2D> transmittance_lut = nullptr;
	std::unique_ptr<Texture2D> multiscatter_lut = nullptr;
	std::unique_ptr<Texture2D> sky_view_lut = nullptr;
	std::unique_ptr<Texture3D> aerial_perspective_lut = nullptr;

	struct PhysSkySB
	{
		DirectX::XMFLOAT3 sun_dir;
		DirectX::XMFLOAT3 player_cam_pos;

		uint32_t enable_sky;
		uint32_t enable_scatter;

		uint32_t transmittance_step;
		uint32_t multiscatter_step;
		uint32_t multiscatter_sqrt_samples;
		uint32_t skyview_step;
		float aerial_perspective_max_dist;

		DirectX::XMFLOAT2 unit_scale;
		float bottom_z;
		float ground_radius;
		float atmos_thickness;

		DirectX::XMFLOAT3 ground_albedo;

		uint32_t limb_darken_model;
		DirectX::XMFLOAT3 sun_intensity;
		float sun_half_angle;

		DirectX::XMFLOAT3 rayleigh_scatter;
		DirectX::XMFLOAT3 rayleigh_absorption;
		float rayleigh_decay;

		DirectX::XMFLOAT3 mie_scatter;
		DirectX::XMFLOAT3 mie_absorption;
		float mie_decay;

		DirectX::XMFLOAT3 ozone_absorption;
		float ozone_height;
		float ozone_thickness;

		float ap_inscatter_mix;
		float ap_transmittance_mix;
		float light_transmittance_mix;
	} phys_sky_sb_content;

	std::unique_ptr<Buffer> phys_sky_sb = nullptr;
	void UpdatePhysSkySB();
	void UpdatePerCameraSB();

	winrt::com_ptr<ID3D11ComputeShader> transmittance_program = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> multiscatter_program = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> sky_view_program = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> aerial_perspective_program = nullptr;

	// rendering
	virtual void SetupResources();
	void CompileShaders();
	void RecompileShaders();
	virtual void Reset(){};

	virtual void Draw(const RE::BSShader* shader, const uint32_t descriptor);
	void GenerateLuts();
	void ModifySky();
	void ModifyLighting();
};