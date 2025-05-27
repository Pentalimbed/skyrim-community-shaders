#pragma once

class WeatherSim
{
	constexpr static uint16_t kCloudW = 256;
	constexpr static uint16_t kCloudH = 256;
	constexpr static uint16_t kCloudD = 256;
	constexpr static uint k3DTexSlot = 4;
	constexpr static uint k3DRWTexSlot = 2;

	float lastDt;
	std::vector<std::function<void()>> tasks = {};
	int updProgress = 0;

	bool enableDebugUpdate = false;
	bool enableDebugViz = true;
	int debugSlice = 30;
	struct Settings
	{
		float tmpOcean = 18;               // Celcius, sea level (z=0) temperature
		float epsConf = .1f;               // vorticity confinement strength
		float2 globalWind = { 0.f, 0.f };  // m s^-1, boundary wind velocity
	} settings;

	struct CB
	{
		float dt;
		float3 debugValues;
		float tmpOcean;  // in K
		float epsConf;
		float2 globalWind;
	};

	eastl::unique_ptr<ConstantBuffer> cb = nullptr;

	eastl::unique_ptr<Texture3D> texU = nullptr;
	eastl::unique_ptr<Texture3D> texQV = nullptr;
	eastl::unique_ptr<Texture3D> texQW = nullptr;
	eastl::unique_ptr<Texture3D> texQC = nullptr;
	eastl::unique_ptr<Texture3D> texQR = nullptr;
	eastl::unique_ptr<Texture3D> texQS = nullptr;
	eastl::unique_ptr<Texture3D> texQI = nullptr;
	eastl::unique_ptr<Texture3D> texTheta = nullptr;
	std::array<eastl::unique_ptr<Texture3D>, 2> tex3dVec3Temps = { nullptr };
	std::array<eastl::unique_ptr<Texture3D>, 7> tex3dTemps = { nullptr };
	eastl::unique_ptr<Texture2D> texDebug = nullptr;

	winrt::com_ptr<ID3D11ComputeShader> csInitStates = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csGroundEva = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csAdvectVel = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csDiffVelIter = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csVorticity = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csVortConfine = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csBuoyExtForce = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csDivVel = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csProjectIter = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csProject = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csAdvectProps = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csWaterMicrophysics = nullptr;
	winrt::com_ptr<ID3D11ComputeShader> csDebugViz = nullptr;

	void BasicDispatch(
		std::span<ID3D11ShaderResourceView*> srv2ds,
		std::span<ID3D11UnorderedAccessView*> uav2ds,
		std::span<ID3D11ShaderResourceView*> srv3ds,
		std::span<ID3D11UnorderedAccessView*> uav3ds,
		ID3D11ComputeShader* cs, bool is3D);

public:
	WeatherSim();

	void DrawSettings();

	void SetupResources();
	void CompileShaders();

	bool Update();
	void InitStates();
	void PerFrame();
};