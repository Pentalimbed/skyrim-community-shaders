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

float GetSoftShadow(float2 uv, float3 dirLightDirectionWS, float startZ, float2 cameraXY, SamplerState samp)
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
	float minFraction = 1e5f;
	float t = 1.f;
	float rcpt = 1.f;
	for (uint i = perPassTerraOcc[0].ShadowSamples; i > 0; i--) {
		float2 currUV = uv + dRayEndUV * t;
		if (all((currUV - uv) * dims < 1))
			break;
		float rayZ = startZ + dRayEndWS.z * t;
		float dz = rayZ - TexTerraOcc.SampleLevel(samp, currUV, 0).z + perPassTerraOcc[0].ShadowBias;
		float fraction = dz * rcpt;
		minFraction = min(fraction, minFraction);

		t *= .5f;
		rcpt *= 2.f;
	}

	float shadowFraction = atan2(minFraction * abs(bitangent.z), perPassTerraOcc[0].ShadowMaxDistance);
	shadowFraction = 0.5 + shadowFraction * perPassTerraOcc[0].ShadowSoftening;

	return saturate(shadowFraction);
}