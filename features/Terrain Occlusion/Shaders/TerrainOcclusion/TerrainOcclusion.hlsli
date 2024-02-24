struct PerPassTerraOcc
{
	uint EnableTerrainShadow;
	uint EnableTerrainAO;

	float ShadowBias;
	float ShadowSoftening;
	float ShadowMaxDistance;
	float ShadowAnglePower;
	uint ShadowSamples;

	float AOAmbientMix;
	float AODiffuseMix;
	float AOPower;
	float AOFadeOutHeightRcp;

	float3 scale;
	float3 invScale;
	float3 offset;
};

StructuredBuffer<PerPassTerraOcc> perPassTerraOcc : register(t25);
Texture2D<float4> TexTerraOcc : register(t26);

float2 GetTerrainOcclusionUV(float2 xy)
{
	return xy * perPassTerraOcc[0].scale.xy + perPassTerraOcc[0].offset.xy;
}

float2 GetTerrainOcclusionXY(float2 uv)
{
	return (uv - perPassTerraOcc[0].offset.xy) * perPassTerraOcc[0].invScale.xy;
}

float GetTerrainSoftShadow(float2 uv, float3 dirLightDirectionWS, float startZ, SamplerState samp)
{
	if (dirLightDirectionWS.z < 1e-3)
		return 1;

	uint2 dims;
	TexTerraOcc.GetDimensions(dims.x, dims.y);

	float3 dirLightDir = normalize(float3(dirLightDirectionWS.xy, pow(dirLightDirectionWS.z, perPassTerraOcc[0].ShadowAnglePower)));
	float3 tangent = cross(dirLightDir, float3(0, 0, 1));
	float3 bitangent = cross(dirLightDir, tangent);

	float3 dRayEndWS = dirLightDir / length(dirLightDir.xy) * perPassTerraOcc[0].ShadowMaxDistance;
	float2 dRayEndUV = dRayEndWS.xy * perPassTerraOcc[0].scale.xy;

	const float rcpN = rcp(perPassTerraOcc[0].ShadowSamples);
	float maxFraction = 0.f;
	float t = rcpN;
	for (uint i = 0; i < perPassTerraOcc[0].ShadowSamples; i++) {
		float2 currUV = uv + dRayEndUV * t;
		if (all((currUV - uv) * dims < 1))
			break;

		float dz = TexTerraOcc.SampleLevel(samp, currUV, 0).z + perPassTerraOcc[0].ShadowBias - startZ;
		float dworld = t * dRayEndWS.xy;
		if (dz > maxFraction * dworld)
			maxFraction = dz / dworld;

		t += rcpN;
	}

	float shadowFraction = asin(dirLightDir.z) - atan(maxFraction);
	shadowFraction = 0.5 + shadowFraction * perPassTerraOcc[0].ShadowSoftening;

	return saturate(shadowFraction);
}