struct PerPassTerraOcc
{
	uint EnableTerrainShadow;
	uint EnableTerrainAO;

	float ShadowBias;
	float ShadowSofteningRadiusAngle;
	float ShadowMinStep;
	// float ShadowMaxDistance;
	float ShadowAnglePower;
	uint ShadowSamples;

	float AOAmbientMix;
	float AODiffuseMix;
	float AOPower;
	float AOFadeOutHeightRcp;

	float3 scale;
	float3 invScale;
	float3 offset;

	float ShadowSofteningDiameterRcp;
	float AoDistance;
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
	uint2 dims;
	TexTerraOcc.GetDimensions(dims.x, dims.y);
	float2 pxSize = rcp(dims);

	float3 dirLightDir = normalize(float3(dirLightDirectionWS.xy, pow(dirLightDirectionWS.z, perPassTerraOcc[0].ShadowAnglePower)));

	if (dirLightDir.z < 0)
		return 0;
	if (dirLightDir.z > 1 - 1e-3)
		return 1;

	float dirLightAngle = asin(dirLightDir.z);
	float lowerAngle = dirLightAngle - perPassTerraOcc[0].ShadowSofteningRadiusAngle;

	float3 xyzDir = dirLightDir / length(dirLightDir.xy);
	float3 uvzIncrement = float3(xyzDir.xy * perPassTerraOcc[0].scale.xy, max(1e-2, tan(lowerAngle)));

	float viewFraction = 0.f;  // delta z / distance

	const float minDt = perPassTerraOcc[0].ShadowMinStep;
	const float maxDt = perPassTerraOcc[0].AoDistance;
	float t = minDt;  // in world distance
	float3 currUVZ = float3(uv, startZ) + t * uvzIncrement;

	for (uint i = 0; i < perPassTerraOcc[0].ShadowSamples; i++) {
		float4 currSample = TexTerraOcc.SampleLevel(samp, currUVZ.xy, 0);
		float currConeCot = currSample.y;
		float currGroundZ = currSample.z + perPassTerraOcc[0].ShadowBias;

		float diffZ = currUVZ.z - currGroundZ;

		float dt = minDt;
		if (diffZ < 0) {  // intersect
			viewFraction = (currGroundZ - startZ) / t;
			if (viewFraction > currConeCot)
				dt = maxDt;

			currUVZ.z = currGroundZ;
			uvzIncrement.z = viewFraction;
		} else {  // find cone boundary
			if (currGroundZ - startZ > 5e4)
				break;

			dt = diffZ / (currConeCot - viewFraction);
			dt = clamp(dt, minDt, maxDt);
		}

		t += dt;
		currUVZ += dt * uvzIncrement;

		if (any(currUVZ.xy < 0) || any(currUVZ.xy) > 1)
			break;
	}

	// return i * rcp(perPassTerraOcc[0].ShadowSamples);

	float shadowFraction = asin(dirLightDir.z) - atan(viewFraction);
	shadowFraction = 0.5 + shadowFraction * perPassTerraOcc[0].ShadowSofteningDiameterRcp;

	return saturate(shadowFraction);
}