struct PerPassTerraOcc
{
	uint EnableTerrainOcclusion;

	float AOAmbientMix;
	float AODirectMix;
	float AOPower;
	float AOFadeOutHeightRcp;

	float3 scale;
	float3 offset;
};

StructuredBuffer<PerPassTerraOcc> perPassTerraOcc : register(t25);
Texture2D<float4> TexTerraOcc : register(t26);

float2 GetTerrainOcclusionUv(float2 xy)
{
	return xy * perPassTerraOcc[0].scale.xy + perPassTerraOcc[0].offset.xy;
}