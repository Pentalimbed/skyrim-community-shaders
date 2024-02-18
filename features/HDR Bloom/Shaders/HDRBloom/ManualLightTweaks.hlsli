struct ManualLightTweaks
{
	float DirLightPower;
	float LightPower;
	float AmbientPower;
	float EmitPower;
};

StructuredBuffer<ManualLightTweaks> manualLightTweaks : register(t41);

float3 LightPower(float3 color, float power)
{
	float len = length(color);
	if (len < 1e-5)
		return color;
	return color * pow(length(color), power - 1);
}