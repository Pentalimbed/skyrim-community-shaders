struct PerPassTerraOcc
{
	uint EnableTerrainShadow;
	uint EnableTerrainAO;

	float ShadowSoftening;

	float AOAmbientMix;
	float AODiffuseMix;
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

float GetSoftShadow(float2 uv, float3 dirLightDirectionWS, float startZ, SamplerState samp)
{
	float2 vLightRayTS = dirLightDirectionWS.xy * 0.01;

	float shadowSoftening = perPassTerraOcc[0].ShadowSoftening;

	// Compute the soft blurry shadows taking into account self-occlusion for
	// features of the height field:

	float sh0 = startZ;
	float shA = (TexTerraOcc.SampleLevel(samp, uv + vLightRayTS * 0.88, 0).z - sh0 - 0.88) * 1 * shadowSoftening;
	float sh9 = (TexTerraOcc.SampleLevel(samp, uv + vLightRayTS * 0.77, 0).z - sh0 - 0.77) * 2 * shadowSoftening;
	float sh8 = (TexTerraOcc.SampleLevel(samp, uv + vLightRayTS * 0.66, 0).z - sh0 - 0.66) * 4 * shadowSoftening;
	float sh7 = (TexTerraOcc.SampleLevel(samp, uv + vLightRayTS * 0.55, 0).z - sh0 - 0.55) * 6 * shadowSoftening;
	float sh6 = (TexTerraOcc.SampleLevel(samp, uv + vLightRayTS * 0.44, 0).z - sh0 - 0.44) * 8 * shadowSoftening;
	float sh5 = (TexTerraOcc.SampleLevel(samp, uv + vLightRayTS * 0.33, 0).z - sh0 - 0.33) * 10 * shadowSoftening;
	float sh4 = (TexTerraOcc.SampleLevel(samp, uv + vLightRayTS * 0.22, 0).z - sh0 - 0.22) * 12 * shadowSoftening;

	// Compute the actual shadow strength:
	float fOcclusionShadow = 1 - max(max(max(max(max(max(shA, sh9), sh8), sh7), sh6), sh5), sh4);
	return fOcclusionShadow;
}