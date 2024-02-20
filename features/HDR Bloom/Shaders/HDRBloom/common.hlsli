/// By ProfJack/五脚猫, 2024-2-17 UTC

float Luma(float3 color)
{
	return dot(color, float3(0.2126, 0.7152, 0.0722));
}

/*------------ COLOR GRADING ------------*/

float3 ASC_CDL(float3 col, float3 slope, float3 power, float3 offset)
{
	return pow(col * slope + offset, power);
}

float3 Saturate(float3 col, float3 sat)
{
	float luma = Luma(col);
	return lerp(luma, col, sat);
}

/*  AgX Reference:
 *  AgX by longbool https://www.shadertoy.com/view/dtSGD1
 *  AgX Minimal by bwrensch https://www.shadertoy.com/view/cd3XWr
 *  Fork AgX Minima troy_s 342 by troy_s https://www.shadertoy.com/view/mdcSDH
 */

// Mean error^2: 3.6705141e-06
float3 AgxDefaultContrastApprox5(float3 x)
{
	float3 x2 = x * x;
	float3 x4 = x2 * x2;

	return +15.5 * x4 * x2 - 40.14 * x4 * x + 31.96 * x4 - 6.868 * x2 * x +
	       0.4298 * x2 + 0.1191 * x - 0.00232;
}

// Mean error^2: 1.85907662e-06
float3 AgxDefaultContrastApprox6(float3 x)
{
	float3 x2 = x * x;
	float3 x4 = x2 * x2;

	return -17.86 * x4 * x2 * x + 78.01 * x4 * x2 - 126.7 * x4 * x + 92.06 * x4 -
	       28.72 * x2 * x + 4.361 * x2 - 0.1718 * x + 0.002857;
}

float3 Agx(float3 val)
{
	const float3x3 agx_mat = transpose(
		float3x3(0.842479062253094, 0.0423282422610123, 0.0423756549057051,
			0.0784335999999992, 0.878468636469772, 0.0784336,
			0.0792237451477643, 0.0791661274605434, 0.879142973793104));

	const float min_ev = -12.47393f;
	const float max_ev = 4.026069f;

	// Input transform
	val = mul(agx_mat, val);

	// Log2 space encoding
	val = clamp(log2(val), min_ev, max_ev);
	val = (val - min_ev) / (max_ev - min_ev);

	// Apply sigmoid function approximation
	val = AgxDefaultContrastApprox6(val);

	return val;
}

float3 AgxEotf(float3 val)
{
	const float3x3 agx_mat_inv = transpose(
		float3x3(1.19687900512017, -0.0528968517574562, -0.0529716355144438,
			-0.0980208811401368, 1.15190312990417, -0.0980434501171241,
			-0.0990297440797205, -0.0989611768448433, 1.15107367264116));

	// Undo input transform
	val = mul(agx_mat_inv, val);

	// sRGB IEC 61966-2-1 2.2 Exponent Reference EOTF Display
	val = pow(saturate(val), 2.2);

	return val;
}

// ref:
// https://advances.realtimerendering.com/s2021/jpatry_advances2021/index.html#/167/0/1
// https://github.com/google/filament
// https://www.shadertoy.com/view/ft3Sz7
float4 RGB2LMSR(float3 c)
{
	const float4x3 m = float4x3(
						   0.31670331, 0.70299344, 0.08120592,
						   0.10129085, 0.72118661, 0.12041039,
						   0.01451538, 0.05643031, 0.53416779,
						   0.01724063, 0.60147464, 0.40056206) *
	                   24.303;
	return mul(m, c);
}

float3 LMS2RGB(float3 c)
{
	const float3x3 m = float3x3(
						   4.57829597, -4.48749114, 0.31554848,
						   -0.63342362, 2.03236026, -0.36183302,
						   -0.05749394, -0.09275939, 1.90172089) /
	                   24.303;
	return mul(m, c);
}

// https://www.ncbi.nlm.nih.gov/pmc/articles/PMC2630540/pdf/nihms80286.pdf
float3 PurkinjeShift(float3 c, float nightAdaptation)
{
	const static float3 m = float3(0.63721, 0.39242, 1.6064);
	const static float K = 45.0;
	const static float S = 10.0;
	const static float k3 = 0.6;
	const static float k5 = 0.2;
	const static float k6 = 0.29;
	const static float rw = 0.139;
	const static float p = 0.6189;

	const static float logExposure = 380.0f;

	float4 lmsr = RGB2LMSR(c * logExposure);

	float3 g = 1 / sqrt(1 + (.33 / m) * (lmsr.xyz + float3(k5, k5, k6) * lmsr.w));

	float rc_gr = (K / S) * ((1.0 + rw * k3) * g.y / m.y - (k3 + rw) * g.x / m.x) * k5 * lmsr.w;
	float rc_by = (K / S) * (k6 * g.z / m.z - k3 * (p * k5 * g.x / m.x + (1.0 - p) * k5 * g.y / m.y)) * lmsr.w;
	float rc_lm = K * (p * g.x / m.x + (1.0 - p) * g.y / m.y) * k5 * lmsr.w;

	float3 lms_gain = float3(-0.5 * rc_gr + 0.5 * rc_lm, 0.5 * rc_gr + 0.5 * rc_lm, rc_by + rc_lm) * nightAdaptation;
	float3 rgb_gain = LMS2RGB(lmsr + lms_gain) / logExposure;

	return rgb_gain;
}