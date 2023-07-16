#pragma once

#include "Buffer.h"
#include "Feature.h"

struct Orbit
{
	// in rad
	float azimuth = 0;  // E-W
	float zenith = 20 * RE::NI_PI / 180.0;
	// in [-1, 1]
	float offset = 0;

	RE::NiPoint3 getDir(float t);  // t = fraction of a cycle, start at the bottom
	RE::NiPoint3 getTangent(float t);
};

struct Trajectory
{
	Orbit minima, maxima;
	// in days
	float period_dirunal = 1;  // circling one orbit
	float offset_dirunal = 0;  // add to gameDaysPassed
	float period_shift = 364;  // from minima to maxima and back
	float offset_shift = 0;    // start from the mean of minima and maxima

	Orbit getMixedOrbit(float gameDaysPassed);
	RE::NiPoint3 getDir(float gameDaysPassed);
	RE::NiPoint3 getTangent(float gameDaysPassed);
};

struct PhysicalWeather : Feature
{
	// boilerplates
	inline static auto* GetSingleton()
	{
		static PhysicalWeather singleton;
		return &singleton;
	}

	virtual inline std::string GetName() { return "Physical Weather"; }
	virtual inline std::string GetShortName() { return "PhysicalWeather"; }

	// params
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
	struct Settings
	{
		bool enable_sky = true;
		bool enable_scatter = true;
		bool enable_tonemap = true;
		float tonemap_keyval = 1.f;

		// PERFORMANCE
		uint32_t transmittance_step = 40;
		uint32_t multiscatter_step = 20;
		uint32_t multiscatter_sqrt_samples = 4;
		uint32_t skyview_step = 30;
		float aerial_perspective_max_dist = 50;  // in km

		// WORLD
		DirectX::XMFLOAT2 unit_scale = { 10, 1 };
		float bottom_z = -15000;       // in game unit
		float ground_radius = 6.36f;   // in megameter
		float atmos_thickness = .05f;  // 0.1
		DirectX::XMFLOAT3 ground_albedo = { .3f, .3f, .3f };

		DirectX::XMFLOAT3 sunlight_color = { 100, 100, 100 };
		DirectX::XMFLOAT3 moonlight_color = { 1, 1, 1 };

		// ORBITS
		bool override_light_color = false;
		bool override_light_dir = false;
		float critcial_sun_angle = 10 * RE::NI_PI / 180.0;

		Trajectory sun_trajectory;
		Trajectory masser_trajectory = {
			.minima = { .zenith = 0, .offset = -.174 },
			.maxima = { .zenith = 0, .offset = -.174 },
			.offset_dirunal = .456
		};
		Trajectory secunda_trajectory = {
			.minima = { .zenith = 0, .offset = -.446 },
			.maxima = { .zenith = 0, .offset = -.446 },
			.offset_dirunal = .403
		};

		// CELESTIALS
		int32_t limb_darken_model = 1;
		float limb_darken_power = 1.f;
		DirectX::XMFLOAT3 sun_color = { 100, 100, 100 };     // 1.69e9 cd m^-2
		float sun_aperture_angle = 2.2 * RE::NI_PI / 180.0;  // in rad

		float masser_aperture_angle = 10 * RE::NI_PI / 180.0;
		float masser_brightness = 1;

		float secunda_aperture_angle = 4.5 * RE::NI_PI / 180.0;
		float secunda_brightness = 1;

		float stars_brightness = 1;

		// ATMOSPHERE
		DirectX::XMFLOAT3 rayleigh_scatter = { 5.802f, 13.558f, 33.1f };  // in megameter^-1
		DirectX::XMFLOAT3 rayleigh_absorption = { 0.f, 0.f, 0.f };
		float rayleigh_decay = 8.f;  // in km^-1

		int32_t mie_phase_func = 1;
		float mie_asymmetry = 0.85;  // the g, 0.8
		DirectX::XMFLOAT3 mie_scatter = { 3.996f, 3.996f, 3.996f };
		DirectX::XMFLOAT3 mie_absorption = { .444f, .444f, .444f };
		float mie_decay = 1.2f;

		DirectX::XMFLOAT3 ozone_absorption = { 0.650f, 1.881f, 0.085f };
		float ozone_height = 25.f;  // in km
		float ozone_thickness = 30.f;

		float ap_inscatter_mix = 1.f;
		float ap_transmittance_mix = 1.f;
		float light_transmittance_mix = 1.f;
	} settings;

	virtual void DrawSettings();
	void DrawSettingsGeneral();
	void DrawSettingsQuality();
	void DrawSettingsWorld();
	void DrawSettingsOrbits();
	void DrawSettingsAtmosphere();
	void DrawSettingsCelestials();
	void DrawSettingsDebug();

	virtual void Load(json& o_json);
	virtual void Save(json& o_json);

	void Update();
	void UpdateOrbits();

	// resources
	std::unique_ptr<Texture2D> transmittance_lut = nullptr;
	std::unique_ptr<Texture2D> multiscatter_lut = nullptr;
	std::unique_ptr<Texture2D> sky_view_lut = nullptr;
	std::unique_ptr<Texture3D> aerial_perspective_lut = nullptr;

	ID3D11ShaderResourceView* srv_galaxy = nullptr;

	struct PhysWeatherSB
	{
		float timer = 0;
		DirectX::XMFLOAT3 sun_dir;
		DirectX::XMFLOAT3 masser_dir;
		DirectX::XMFLOAT3 masser_upvec;
		DirectX::XMFLOAT3 secunda_dir;
		DirectX::XMFLOAT3 secunda_upvec;
		// DirectX::XMFLOAT3X3 galaxy_rotate;
		DirectX::XMFLOAT3 player_cam_pos;

		uint32_t enable_sky;
		uint32_t enable_scatter;
		uint32_t enable_tonemap;
		float tonemap_keyval;

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

		DirectX::XMFLOAT3 dirlight_color;
		DirectX::XMFLOAT3 dirlight_dir;

		uint32_t limb_darken_model;
		float limb_darken_power;
		DirectX::XMFLOAT3 sun_color;
		float sun_aperture_cos;

		float masser_aperture_cos;
		float masser_brightness;

		float secunda_aperture_cos;
		float secunda_brightness;

		float stars_brightness;

		DirectX::XMFLOAT3 rayleigh_scatter;
		DirectX::XMFLOAT3 rayleigh_absorption;
		float rayleigh_decay;

		uint32_t mie_phase_func;
		float mie_asymmetry;
		DirectX::XMFLOAT3 mie_scatter;
		DirectX::XMFLOAT3 mie_absorption;
		float mie_decay;

		DirectX::XMFLOAT3 ozone_absorption;
		float ozone_height;
		float ozone_thickness;

		float ap_inscatter_mix;
		float ap_transmittance_mix;
		float light_transmittance_mix;
	} phys_weather_sb_content;

	std::unique_ptr<Buffer> phys_weather_sb = nullptr;
	void UploadPhysWeatherSB();

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
	void ModifySky(const RE::BSShader* shader, const uint32_t descriptor);
	void ModifyLighting();
};