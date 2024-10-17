#pragma once

#include "Buffer.h"
#include "Feature.h"

//////////////////////////////////////////////////////////////////////////

struct Orbit
{
	// in rad
	float azimuth = 0;  // rad, 0 being E-W
	float zenith = 0;   // rad, 0 being up
	// in [-1, 1]
	float drift = 7.f * RE::NI_PI / 180.f;  // rad, moving to the sides

	RE::NiPoint3 getDir(float t);  // t = fraction of a cycle, start at the bottom
	RE::NiPoint3 getTangent(float t);
};

struct Trajectory
{
	Orbit minima, maxima;
	// in days
	float period_orbital = 1;  // circling one orbit
	float offset_orbital = 0;  // add to gameDaysPassed
	float period_long = 364;   // from minima to maxima and back
	float offset_long = -10;   // start from the mean of minima and maxima

	Orbit getMixedOrbit(float gameDaysPassed);
	RE::NiPoint3 getDir(float gameDaysPassed);
	RE::NiPoint3 getTangent(float gameDaysPassed);
};

struct PhysicalSky : public Feature
{
	constexpr static uint16_t s_transmittance_width = 256;
	constexpr static uint16_t s_transmittance_height = 64;
	constexpr static uint16_t s_multiscatter_width = 32;
	constexpr static uint16_t s_multiscatter_height = 32;
	constexpr static uint16_t s_sky_view_width = 200;
	constexpr static uint16_t s_sky_view_height = 150;
	constexpr static uint16_t s_aerial_perspective_width = 32;
	constexpr static uint16_t s_aerial_perspective_height = 32;
	constexpr static uint16_t s_aerial_perspective_depth = 32;

	static PhysicalSky* GetSingleton()
	{
		static PhysicalSky singleton;
		return std::addressof(singleton);
	}

	virtual inline std::string GetName() override { return "Physical Sky"; }
	virtual inline std::string GetShortName() override { return "PhysicalSky"; }
	virtual inline std::string_view GetShaderDefineName() override { return "PHYS_SKY"; }
	virtual bool HasShaderDefine(RE::BSShader::Type) override;

	uint32_t current_sky_obj_type = 0;  // 1-sun, 2-masser, 3-secunda
	uint32_t current_moon_phases[2];

	struct Settings
	{
		// GENRERAL
		bool enable_sky = false;
		bool enable_aerial = true;

		// PERFORMANCE
		uint transmittance_step = 40;
		uint multiscatter_step = 20;
		uint multiscatter_sqrt_samples = 4;
		uint skyview_step = 30;
		float aerial_perspective_max_dist = 80;  // in km

		// WORLD
		float unit_scale = 5;
		float bottom_z = -15000;        // in game unit
		float planet_radius = 6.36e3f;  // 6360 km
		float atmos_thickness = 100.f;  // 20 km
		float3 ground_albedo = { .2f, .2f, .2f };

		// LIGHTING
		float3 sunlight_color = float3{ 1.0f, 0.949f, 0.937f } * 15.f;

		float3 moonlight_color = float3{ .9f, .9f, 1.f } * .1f;
		bool phase_dep_moonlight = false;
		float masser_moonlight_min = 0.1f;
		float3 masser_moonlight_color = float3{ .1f, .07f, .07f };
		float secunda_moonlight_min = 0.1f;
		float3 secunda_moonlight_color = float3{ .9f, .9f, 1.f } * .2f;

		float2 light_transition_angles = float2{ -10.f, -14.f } * RE::NI_PI / 180.0;

		bool enable_vanilla_clouds = true;
		float cloud_height = 4.f;  // km
		float cloud_saturation = .7f;
		float cloud_mult = 1.f;
		float cloud_atmos_scatter = 3.f;

		float cloud_phase_g0 = .42f;
		float cloud_phase_g1 = -.3f;
		float cloud_phase_w = .3f;
		float cloud_alpha_heuristics = 0.3f;
		float cloud_color_heuristics = 0.1f;

		bool override_dirlight_color = false;
		bool override_dirlight_dir = false;
		bool moonlight_follows_secunda = true;
		float dirlight_transmittance_mix = 0;
		float treelod_saturation = .1f;
		float treelod_mult = .25f;

		// CELESTIALS
		bool override_vanilla_celestials = true;
		// - Sun
		float3 sun_disc_color = float3{ 1.f, 0.949f, 0.937f } * 50.f;
		float sun_angular_radius = .5f * RE::NI_PI / 180.0f;  // in rad

		bool override_sun_traj = false;
		Trajectory sun_trajectory{
			.minima = { .zenith = -40 * RE::NI_PI / 180.0f, .drift = -23.5f * RE::NI_PI / 180.0f },
			.maxima = { .zenith = -40 * RE::NI_PI / 180.0f, .drift = 23.5f * RE::NI_PI / 180.0f }
		};

		// - Moons
		bool override_masser_traj = false;
		Trajectory masser_trajectory = {
			.minima = { .zenith = 0, .drift = -.174f },
			.maxima = { .zenith = 0, .drift = -.174f },
			.period_orbital = .25f / 0.23958333333f,  // po3 values :)
			.offset_orbital = 0.7472f
		};
		float masser_angular_radius = 10.f * RE::NI_PI / 180.0f;
		float masser_brightness = .7f;

		bool override_secunda_traj = false;
		Trajectory secunda_trajectory = {
			.minima = { .zenith = 0, .drift = -.423f },
			.maxima = { .zenith = 0, .drift = -.423f },
			.period_orbital = .25f / 0.2375f,
			.offset_orbital = 0.3775f
		};
		float secunda_angular_radius = 4.5f * RE::NI_PI / 180.0f;
		float secunda_brightness = .7f;

		float stars_brightness = 1;

		// ATMOSPHERE
		float ap_inscatter_mix = 1.f;
		float ap_transmittance_mix = 1.f;

		float3 rayleigh_scatter = { 6.6049f, 12.345f, 29.413f };  // in megameter^-1
		float3 rayleigh_absorption = { 0.f, 0.f, 0.f };
		float rayleigh_decay = 1 / 8.69645f;  // in km^-1

		float aerosol_phase_func_g = 0.8f;
		float3 aerosol_scatter = { 3.996f, 3.996f, 3.996f };
		float3 aerosol_absorption = { .444f, .444f, .444f };
		float aerosol_decay = 1 / 1.2f;

		float3 ozone_absorption = { 2.2911f, 1.5404f, 0 };
		float ozone_altitude = 22.3499f + 35.66071f * .5f;  // in km
		float ozone_thickness = 35.66071f;

	} settings;

	struct PhysSkySB
	{
		uint enable_sky;
		uint enable_aerial;

		// PERFORMANCE
		uint transmittance_step;
		uint multiscatter_step;
		uint multiscatter_sqrt_samples;
		uint skyview_step;
		float aerial_perspective_max_dist;

		// WORLD
		float unit_scale;
		float bottom_z;
		float planet_radius;
		float atmos_thickness;
		float3 ground_albedo;

		// LIGHTING
		uint override_dirlight_color;
		float dirlight_transmittance_mix;
		float treelod_saturation;
		float treelod_mult;

		uint enable_vanilla_clouds;
		float cloud_height;
		float cloud_saturation;
		float cloud_mult;
		float cloud_atmos_scatter;

		float cloud_phase_g0;
		float cloud_phase_g1;
		float cloud_phase_w;
		float cloud_alpha_heuristics;
		float cloud_color_heuristics;

		// CELESTIAL
		uint override_vanilla_celestials;
		float3 sun_disc_color;
		float sun_aperture_cos;
		float sun_aperture_rcp_sin;

		float masser_aperture_cos;
		float masser_brightness;

		float secunda_aperture_cos;
		float secunda_brightness;

		// ATMOSPHERE
		float ap_inscatter_mix;
		float ap_transmittance_mix;

		float3 rayleigh_scatter;
		float3 rayleigh_absorption;
		float rayleigh_decay;

		float aerosol_phase_func_g;
		float3 aerosol_scatter;
		float3 aerosol_absorption;
		float aerosol_decay;

		float3 ozone_absorption;
		float ozone_altitude;
		float ozone_thickness;

		// DYNAMIC
		float3 dirlight_dir;
		float3 dirlight_color;
		float3 sun_dir;
		float3 masser_dir;
		float3 masser_upvec;
		float3 secunda_dir;
		float3 secunda_upvec;

		float horizon_penumbra;

		float cam_height_km;  // in km

	} phys_sky_sb_data;
	std::unique_ptr<StructuredBuffer> phys_sky_sb = nullptr;

	struct SkyPerGeometrySB
	{
		uint sky_object_type;
	};
	std::unique_ptr<StructuredBuffer> sky_per_geo_sb = nullptr;

	std::unique_ptr<Texture2D> transmittance_lut = nullptr;
	std::unique_ptr<Texture2D> multiscatter_lut = nullptr;
	std::unique_ptr<Texture2D> sky_view_lut = nullptr;
	std::unique_ptr<Texture3D> aerial_perspective_lut = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> transmittance_program = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> multiscatter_program = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> sky_view_program = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> aerial_perspective_program = nullptr;

	winrt::com_ptr<ID3D11SamplerState> transmittance_sampler = nullptr;
	winrt::com_ptr<ID3D11SamplerState> sky_view_sampler = nullptr;

	virtual void LoadSettings(json& o_json) override;
	virtual void SaveSettings(json& o_json) override;

	virtual void SetupResources() override;
	void CompileComputeShaders();

	inline bool CheckComputeShaders()
	{
		bool result = transmittance_program && multiscatter_program && sky_view_program;
		if (settings.enable_aerial)
			result = result && aerial_perspective_program;
		return result;
	}
	bool NeedLutsUpdate();

	virtual void Reset() override;
	void UpdateBuffer();
	void UpdateOrbitsAndHeight();

	virtual void DrawSettings() override;
	void SettingsGeneral();
	void SettingsWorld();
	void SettingsLighting();
	void SettingsClouds();
	void SettingsCelestials();
	void SettingsAtmosphere();
	void SettingsDebug();

	virtual void Prepass() override;
	void GenerateLuts();

	virtual inline void RestoreDefaultSettings() override { settings = {}; };
	virtual void ClearShaderCache() override;

	virtual inline void PostPostLoad() override { Hooks::Install(); }

	struct Hooks
	{
		struct BSSkyShader_SetupGeometry
		{
			static void thunk(RE::BSShader* a_this, RE::BSRenderPass* a_pass, uint32_t a_renderFlags);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		struct NiPoint3_Normalize
		{
			// at Sun::Update
			static void thunk(RE::NiPoint3* a_this);
			static inline REL::Relocation<decltype(thunk)> func;
		};

		static void Install()
		{
			stl::write_thunk_call<NiPoint3_Normalize>(REL::RelocationID(25798, 26352).address() + REL::Relocate(0x6A8, 0x753));
			// stl::write_vfunc<0x6, BSSkyShader_SetupGeometry>(RE::VTABLE_BSSkyShader[0]);
		}
	};
};