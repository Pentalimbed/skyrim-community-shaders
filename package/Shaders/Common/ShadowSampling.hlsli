#include "Common/Color.hlsl"

struct PerGeometry
{
	float4 VPOSOffset;
	float4 ShadowSampleParam;    // fPoissonRadiusScale / iShadowMapResolution in z and w
	float4 EndSplitDistances;    // cascade end distances int xyz, cascade count int z
	float4 StartSplitDistances;  // cascade start ditances int xyz, 4 int z
	float4 FocusShadowFadeParam;
	float4 DebugColor;
	float4 PropertyColor;
	float4 AlphaTestRef;
	float4 ShadowLightParam;  // Falloff in x, ShadowDistance squared in z
	float4x3 FocusShadowMapProj[4];
	float4x3 ShadowMapProj[4];
	float4x4 CameraViewProjInverse;
};

Texture2DArray<float4> TexShadowMapSampler : register(t25);
StructuredBuffer<PerGeometry> perShadow : register(t26);

#if defined(WATER) || defined(EFFECT)

cbuffer PerWaterType : register(b7)
{
	float3 ShallowColorNoWeather;
	uint pad0water;
	float3 DeepColorNoWeather;
	uint pad1water;
};

float3 GetShadow(float3 positionWS)
{
	PerGeometry sD = perShadow[0];
	sD.EndSplitDistances.x = GetScreenDepth(sD.EndSplitDistances.x);
	sD.EndSplitDistances.y = GetScreenDepth(sD.EndSplitDistances.y);
	sD.EndSplitDistances.z = GetScreenDepth(sD.EndSplitDistances.z);
	sD.EndSplitDistances.w = GetScreenDepth(sD.EndSplitDistances.w);

	float shadowMapDepth = length(positionWS.xyz);

	half cascadeIndex = 0;
	half4x3 lightProjectionMatrix = sD.ShadowMapProj[0];
	half shadowMapThreshold = sD.AlphaTestRef.y;

	[flatten] if (2.5 < sD.EndSplitDistances.w && sD.EndSplitDistances.y < shadowMapDepth)
	{
		lightProjectionMatrix = sD.ShadowMapProj[2];
		shadowMapThreshold = sD.AlphaTestRef.z;
		cascadeIndex = 2;
	}
	else if (sD.EndSplitDistances.x < shadowMapDepth)
	{
		lightProjectionMatrix = sD.ShadowMapProj[1];
		shadowMapThreshold = sD.AlphaTestRef.z;
		cascadeIndex = 1;
	}

	half3 positionLS = mul(transpose(lightProjectionMatrix), half4(positionWS.xyz, 1)).xyz;
	float deltaZ = positionLS.z - shadowMapThreshold;

	float4 depths = TexShadowMapSampler.GatherRed(LinearSampler, half3(positionLS.xy, cascadeIndex), 0);

	float shadow = dot(depths > deltaZ, 0.25);

	return shadow;
}

float phaseHenyeyGreenstein(float cosTheta, float g)
{
	static const float scale = .25 / 3.1415926535;
	const float g2 = g * g;

	float num = (1.0 - g2);
	float denom = pow(abs(1.0 + g2 - 2.0 * g * cosTheta), 1.5);

	return scale * num / denom;
}

void GetVL(float3 startPosWS, float3 endPosWS, float2 screenPosition, out float scatter, out float transmittance)
{
	const static uint nSteps = 16;
	const static float step = 1.0 / float(nSteps);

	// https://s.campbellsci.com/documents/es/technical-papers/obs_light_absorption.pdf
	// clear water: 0.002 cm^-1 * 1.428 cm/game unit
	const float scatterCoeff = 0.002 * 1.428 * scatterCoeffMult;
	const float absorpCoeff = 0.0002 * 1.428 * absorpCoeffMult;
	const float extinction = scatterCoeff + absorpCoeff;
	// A model for the diffuse attenuation coefficient of downwelling irradiance
	// https://agupubs.onlinelibrary.wiley.com/doi/10.1029/2004JC002275
	const float multiScatterConstant =
		1.428e-2 *
		((1 + 0.005 * acos(abs(SunDir.z)) * 180 / M_PI) * absorpCoeff +
			4.18 * (1 - 0.52 * exp(-10.8 * absorpCoeff)) * scatterCoeff);  // k_d in paper

	float3 worldDir = endPosWS - startPosWS;
	float dist = length(worldDir);

	if (dist < 1e-3) {
		scatter = 1;
		transmittance = 0;
		return;
	}

	worldDir = worldDir / dist;

	const static float isoPhase = .25 / 3.1415926535;
	// float phase = isoPhase;
	float phase = phaseHenyeyGreenstein(dot(SunDir.xyz, worldDir), 0.5);
	float depthRatio = rcp(worldDir.z);
	float distRatio = abs(SunDir.z * depthRatio);

	float noise = InterleavedGradientNoise(screenPosition);

	const float cutoffTransmittance = 1e-2;  // don't go deeper than this
#	if defined(UNDERWATER)
	const float cutoffDist = -log(cutoffTransmittance) / (extinction + 1e-8);
#	else
	const float cutoffDist = -log(cutoffTransmittance) / ((1 + distRatio) * extinction + 1e-8);
#	endif

	float marchDist = min(dist, cutoffDist);
	float sunMarchDist = marchDist * distRatio;
	float marchDepth = marchDist * depthRatio;

#	if defined(WATER_CAUSTICS)
	float2 causticsUVShift = (endPosWS - startPosWS).xy + SunDir.xy * sunMarchDist;
#	endif

	PerGeometry sD = perShadow[0];
	sD.EndSplitDistances.x = GetScreenDepth(sD.EndSplitDistances.x);
	sD.EndSplitDistances.y = GetScreenDepth(sD.EndSplitDistances.y);
	sD.EndSplitDistances.z = GetScreenDepth(sD.EndSplitDistances.z);
	sD.EndSplitDistances.w = GetScreenDepth(sD.EndSplitDistances.w);

	scatter = 0;
	transmittance = 1;
	for (uint i = 0; i < nSteps; ++i) {
		float t = saturate((i + noise.x) * step);

		float sampleTransmittance = exp(-step * marchDist * extinction);
		transmittance *= sampleTransmittance;

		// scattering
		// shadowing
		float shadow = 0;
		float caustics = 1;
		{
			float3 samplePositionWS = startPosWS + worldDir * t * marchDist;
			float shadowMapDepth = length(samplePositionWS.xyz);

			half cascadeIndex = 0;
			half4x3 lightProjectionMatrix = sD.ShadowMapProj[0];
			half shadowMapThreshold = sD.AlphaTestRef.y;

			half shadowRange = sD.EndSplitDistances.x;

			[flatten] if (2.5 < sD.EndSplitDistances.w && sD.EndSplitDistances.y < shadowMapDepth)
			{
				lightProjectionMatrix = sD.ShadowMapProj[2];
				shadowMapThreshold = sD.AlphaTestRef.z;
				cascadeIndex = 2;
				shadowRange = sD.EndSplitDistances.z - sD.EndSplitDistances.y;
			}
			else if (sD.EndSplitDistances.x < shadowMapDepth)
			{
				lightProjectionMatrix = sD.ShadowMapProj[1];
				shadowMapThreshold = sD.AlphaTestRef.z;
				cascadeIndex = 1;
				shadowRange = sD.EndSplitDistances.y - sD.EndSplitDistances.x;
			}

			half3 samplePositionLS = mul(transpose(lightProjectionMatrix), half4(samplePositionWS.xyz, 1)).xyz;

			float2 sampleNoise = frac(noise + i * float2(0.245122333753, 0.430159709002));
			float r = sqrt(sampleNoise.x);
			float theta = 2 * M_PI * sampleNoise.y;
			float2 samplePositionLSOffset;
			sincos(theta, samplePositionLSOffset.y, samplePositionLSOffset.x);
			samplePositionLS.xy += 4.0 * samplePositionLSOffset * r / shadowRange;

			float deltaZ = samplePositionLS.z - shadowMapThreshold;

			float4 depths = TexShadowMapSampler.GatherRed(LinearSampler, half3(samplePositionLS.xy, cascadeIndex), 0);

			shadow = dot(depths > deltaZ, 0.25);
		}

#	if defined(WATER_CAUSTICS)
		if (perPassWaterCaustics[0].EnableWaterCaustics) {
			float2 causticsUV = frac((startPosWS.xy + PosAdjust[0].xy + causticsUVShift * t) * 5e-4);
			caustics = ComputeWaterCaustics(causticsUV);
		}
#	endif

		float sunTransmittance = exp(-sunMarchDist * t * extinction) * shadow * caustics;  // assuming water surface is always level
		float inScatter = scatterCoeff * phase * sunTransmittance;
		inScatter += exp(-multiScatterConstant * marchDepth * t) * shadow * (scatterCoeff + absorpCoeff) * isoPhase;  // multiscatter, no caustics coz it kinda feels too stripey

		scatter += inScatter * (1 - sampleTransmittance) / (extinction + 1e-8) * transmittance;
	}

	transmittance = exp(-dist * (1 + distRatio) * extinction);
}
#endif
