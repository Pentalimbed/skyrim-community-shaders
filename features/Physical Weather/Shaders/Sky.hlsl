struct VS_INPUT
{
	float4 Position : POSITION0;

#if defined(TEX) || defined(HORIZFADE)
	float2 TexCoord : TEXCOORD0;
#endif

	float4 Color : COLOR0;
};

struct VS_OUTPUT
{
	float4 Position : SV_POSITION0;

#if defined(DITHER) && defined(TEX)
	float4 TexCoord0 : TEXCOORD0;
#elif defined(DITHER)
	float2 TexCoord0 : TEXCOORD3;
#elif defined(TEX) || defined(HORIZFADE)
	float2 TexCoord0 : TEXCOORD0;
#endif

#if defined(TEXLERP)
	float2 TexCoord1 : TEXCOORD1;
#endif

#if defined(HORIZFADE)
	float TexCoord2 : TEXCOORD2;
#endif

#if defined(TEX) || defined(DITHER) || defined(HORIZFADE)
	float4 Color : COLOR0;
#endif

	float4 WorldPosition : POSITION1;
	float4 PreviousWorldPosition : POSITION2;
};

#ifdef VSHADER
cbuffer PerGeometry : register(b2)
{
	row_major float4x4 WorldViewProj : packoffset(c0);
	row_major float4x4 World : packoffset(c4);
	row_major float4x4 PreviousWorld : packoffset(c8);
	float3 EyePosition : packoffset(c12);
	float VParams : packoffset(c12.w);
	float4 BlendColor[3] : packoffset(c13);
	float2 TexCoordOff : packoffset(c16);
};

VS_OUTPUT main(VS_INPUT input)
{
	VS_OUTPUT vsout;

	float4 inputPosition = float4(input.Position.xyz, 1.0);

#	if defined(OCCLUSION)

	// Intentionally left blank

#	elif defined(MOONMASK)

	vsout.TexCoord0 = input.TexCoord;
	vsout.Color = float4(VParams.xxx, 1.0);

#	elif defined(HORIZFADE)

	float worldHeight = mul(World, inputPosition).z;
	float eyeHeightDelta = -EyePosition.z + worldHeight;

	vsout.TexCoord0.xy = input.TexCoord;
	vsout.TexCoord2.x = saturate((1.0 / 17.0) * eyeHeightDelta);
	vsout.Color.xyz = BlendColor[0].xyz * VParams;
	vsout.Color.w = BlendColor[0].w;

#	else  // MOONMASK HORIZFADE

#		if defined(DITHER)

#			if defined(TEX)
	vsout.TexCoord0.xyzw = input.TexCoord.xyxy * float4(1.0, 1.0, 501.0, 501.0);
#			else
	float3 inputDirection = normalize(input.Position.xyz);
	inputDirection.y += inputDirection.z;

	vsout.TexCoord0.x = 501 * acos(inputDirection.x);
	vsout.TexCoord0.y = 501 * asin(inputDirection.y);
#			endif  // TEX

#		elif defined(CLOUDS)
	vsout.TexCoord0.xy = TexCoordOff + input.TexCoord;
#		else
	vsout.TexCoord0.xy = input.TexCoord;
#		endif  // DITHER CLOUDS

#		ifdef TEXLERP
	vsout.TexCoord1.xy = TexCoordOff + input.TexCoord;
#		endif  // TEXLERP

	float3 skyColor = BlendColor[0].xyz * input.Color.xxx + BlendColor[1].xyz * input.Color.yyy +
	                  BlendColor[2].xyz * input.Color.zzz;

	vsout.Color.xyz = VParams * skyColor;
	vsout.Color.w = BlendColor[0].w * input.Color.w;

#	endif  // OCCLUSION MOONMASK HORIZFADE

	vsout.Position = mul(WorldViewProj, inputPosition).xyww;
	vsout.WorldPosition = mul(World, inputPosition);
	vsout.PreviousWorldPosition = mul(PreviousWorld, inputPosition);

	return vsout;
}
#endif

typedef VS_OUTPUT PS_INPUT;

struct PS_OUTPUT
{
	float4 Color : SV_Target0;
	float2 MotionVectors : SV_Target1;
	float4 Normal : SV_Target2;
};

#ifdef PSHADER

SamplerState SampBaseSampler : register(s0);
SamplerState SampBlendSampler : register(s1);
SamplerState SampNoiseGradSampler : register(s2);

Texture2D<float4> TexBaseSampler : register(t0);
Texture2D<float4> TexBlendSampler : register(t1);
Texture2D<float4> TexNoiseGradSampler : register(t2);

#	ifdef PHYSICAL_WEATHER
#		include "PhysicalWeather/aurora.hlsli"
#		include "PhysicalWeather/cloud.hlsli"
#		include "PhysicalWeather/common.hlsli"
StructuredBuffer<PhysWeatherSB> phys_weather : register(t16);
Texture2D<float4> TexSkyView : register(t17);
Texture3D<float4> TexAerialPerspective : register(t18);
Texture2D<float4> TexTransmittance : register(t19);
Texture2D<float4> TexMasser : register(t20);
Texture2D<float4> TexSecunda : register(t21);
#	endif

cbuffer PerFrame : register(b12)
{
	float4 UnknownPerFrame1[12] : packoffset(c0);
	row_major float4x4 ScreenProj : packoffset(c12);
	row_major float4x4 PreviousScreenProj : packoffset(c16);
}

cbuffer PerGeometry : register(b2)
{
	float2 PParams : packoffset(c0);
};

cbuffer AlphaTestRefCB : register(b11)
{
	float AlphaTestRefRS : packoffset(c0);
}

PS_OUTPUT main(PS_INPUT input)
{
	PS_OUTPUT psout;
	psout.Color.xyz = 0;

#	ifndef OCCLUSION
#		ifndef TEXLERP  // Clouds | Texture
	float4 baseColor = TexBaseSampler.Sample(SampBaseSampler, input.TexCoord0.xy);
#			ifdef TEXFADE  // CloudsFade
	baseColor.w *= PParams.x;
#			endif
#		else  // CloudsLerp
	float4 blendColor = TexBlendSampler.Sample(SampBlendSampler, input.TexCoord1.xy);
	float4 baseColor = TexBaseSampler.Sample(SampBaseSampler, input.TexCoord0.xy);
	baseColor = PParams.xxxx * (-baseColor + blendColor) + baseColor;
#		endif

#		if defined(DITHER)
	float2 noiseGradUv = float2(0.125, 0.125) * input.Position.xy;
	float noiseGrad =
		TexNoiseGradSampler.Sample(SampNoiseGradSampler, noiseGradUv).x * 0.03125 + -0.0078125;

#			ifdef TEX  // SunGlare
	psout.Color.xyz = (input.Color.xyz * baseColor.xyz + PParams.yyy) + noiseGrad;
	psout.Color.w = baseColor.w * input.Color.w;
#			else                // Sky
	psout.Color.xyz = (PParams.yyy + input.Color.xyz) + noiseGrad;
	psout.Color.w = input.Color.w;
#			endif               // TEX
#		elif defined(MOONMASK)  // MoonAndStarsMask
	psout.Color.xyzw = baseColor;

	if (baseColor.w - AlphaTestRefRS.x < 0) {
		discard;
	}

#		elif defined(HORIZFADE)  // Stars
	psout.Color.xyz = float3(1.5, 1.5, 1.5) * (input.Color.xyz * baseColor.xyz + PParams.yyy);
	psout.Color.w = input.TexCoord2.x * (baseColor.w * input.Color.w);
#		else
	psout.Color.w = input.Color.w * baseColor.w;
	psout.Color.xyz = input.Color.xyz * baseColor.xyz + PParams.yyy;
#		endif

#	else   // SunOcclude
	psout.Color = float4(0, 0, 0, 1.0);
#	endif  // OCCLUSION

#	ifdef PHYSICAL_WEATHER
	if (phys_weather[0].enable_sky) {
		float3 view_dir = normalize(input.WorldPosition.xyz);
		float height = (phys_weather[0].player_cam_pos.z - phys_weather[0].bottom_z) * phys_weather[0].unit_scale * 1.428e-8 + phys_weather[0].ground_radius;

		float cos_sun_view = dot(phys_weather[0].sun_dir, view_dir);
		float cos_masser_view = dot(phys_weather[0].masser_dir, view_dir);
		float cos_secunda_view = dot(phys_weather[0].secunda_dir, view_dir);

		bool is_sky = rayIntersectSphere(float3(0, 0, height), view_dir, phys_weather[0].ground_radius) < 0;
		bool is_sun = (cos_sun_view > phys_weather[0].sun_aperture_cos);
		bool is_masser = cos_masser_view > phys_weather[0].masser_aperture_cos;
		bool is_secunda = cos_secunda_view > phys_weather[0].secunda_aperture_cos;

#		if defined(DITHER) && !defined(TEX)  // SKY
		psout.Color = float4(0, 0, 0, 1.0);

		if (is_sky) {
			if (is_sun) {
				psout.Color.rgb = phys_weather[0].sun_color;

				float norm_dist = sqrt(max(0, 1 - cos_sun_view * cos_sun_view)) * rsqrt(1 - phys_weather[0].sun_aperture_cos * phys_weather[0].sun_aperture_cos);
				float3 darken_factor = limbDarken(norm_dist, phys_weather[0].limb_darken_model);
				psout.Color.rgb *= pow(darken_factor, phys_weather[0].limb_darken_power);
			}
			if (is_masser) {
				float3 rightvec = cross(phys_weather[0].masser_dir, phys_weather[0].masser_upvec);
				float3 disp = normalize(view_dir - phys_weather[0].masser_dir);
				float2 uv = normalize(float2(dot(rightvec, disp), dot(-phys_weather[0].masser_upvec, disp)));
				uv *= sqrt(1 - cos_masser_view * cos_masser_view) * rsqrt(1 - phys_weather[0].masser_aperture_cos * phys_weather[0].masser_aperture_cos);
				uv = uv * .5 + .5;

				float4 samp = TexMasser.Sample(SampBaseSampler, uv);
				psout.Color.rgb = lerp(psout.Color.rgb, samp.rgb * phys_weather[0].masser_brightness, samp.w);
			}
			if (is_secunda) {
				float3 rightvec = cross(phys_weather[0].secunda_dir, phys_weather[0].secunda_upvec);
				float3 disp = normalize(view_dir - phys_weather[0].secunda_dir);
				float2 uv = normalize(float2(dot(rightvec, disp), dot(-phys_weather[0].secunda_upvec, disp)));
				uv *= sqrt(1 - cos_secunda_view * cos_secunda_view) * rsqrt(1 - phys_weather[0].secunda_aperture_cos * phys_weather[0].secunda_aperture_cos);
				uv = uv * .5 + .5;

				float4 samp = TexSecunda.Sample(SampBaseSampler, uv);
				psout.Color.rgb = lerp(psout.Color.rgb, samp.rgb * phys_weather[0].secunda_brightness, samp.w);
			}

			// AURORA
			// float4 aur = smoothstep(0.0.xxxx, 1.5.xxxx, aurora(float3(5, 6, 0).xzy, view_dir.xzy, input.Position.xy, phys_weather[0].timer));
			// psout.Color.rgb += aur.rgb * aur.a * 3;

			psout.Color.a = 1.0;
		}
		// #		elif defined(OCCLUSION) || (defined(DITHER) && defined(TEX))  // sunocclusion and sunglare
		// 		discard;
#		elif defined(CLOUDS)
		discard;
#		elif defined(HORIZFADE)  // stars
		if (is_sun || is_masser || is_secunda)
			discard;
		psout.Color.rgb = baseColor.rgb * baseColor.w * input.TexCoord2.x * phys_weather[0].stars_brightness;
		psout.Color.a = 1.0;
#		else
		discard;
#		endif

#		if defined(CLOUDS)
		// In-atmosphere
		if (phys_weather[0].enable_scatter) {
			uint3 ap_dims;
			TexAerialPerspective.GetDimensions(ap_dims.x, ap_dims.y, ap_dims.z);

			float dist = rayIntersectSphere(float3(0, 0, height), view_dir, phys_weather[0].ground_radius + phys_weather[0].cloud_bottom_height * 1e-3);
			float depth_slice = lerp(.5 / ap_dims.z, 1 - .5 / ap_dims.z, saturate(dist / phys_weather[0].aerial_perspective_max_dist * 1e3));
			float4 ap = TexAerialPerspective.SampleLevel(SampSkyView, float3(cylinderMapAdjusted(view_dir), depth_slice), 0);
			psout.Color.rgb = psout.Color.rgb * lerp(1, ap.w, phys_weather[0].ap_transmittance_mix) + ap.xyz * phys_weather[0].ap_inscatter_mix;
		}
#		else
		// Ex-atmosphere
		float2 trans_uv = getLutUv(float3(0, 0, height), view_dir, phys_weather[0].ground_radius, phys_weather[0].atmos_thickness);
		psout.Color.rgb *= TexTransmittance.SampleLevel(SampBaseSampler, trans_uv, 0).rgb;
#			if defined(DITHER) && !defined(TEX)  // SKY
		psout.Color.rgb += TexSkyView.SampleLevel(SampSkyView, cylinderMapAdjusted(view_dir), 0).rgb;
#			endif
#		endif

		if (phys_weather[0].enable_tonemap) {
			// TONEMAP
			psout.Color.rgb = jodieReinhardTonemap(psout.Color.xyz * phys_weather[0].tonemap_keyval);

#		if defined(DITHER)
			psout.Color.rgb += noiseGrad;
#		endif
		}
	}
#	endif

	float4 screenPosition = mul(ScreenProj, input.WorldPosition);
	screenPosition.xy = screenPosition.xy / screenPosition.ww;
	float4 previousScreenPosition = mul(PreviousScreenProj, input.PreviousWorldPosition);
	previousScreenPosition.xy = previousScreenPosition.xy / previousScreenPosition.ww;
	float2 screenMotionVector = float2(-0.5, 0.5) * (screenPosition.xy - previousScreenPosition.xy);

	psout.MotionVectors = screenMotionVector;
	psout.Normal = float4(0.5, 0.5, 0, 0);

	return psout;
}
#endif
