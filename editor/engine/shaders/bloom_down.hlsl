#include "Common.hlsli"

// Halves the image for the bloom (see bloom_math.h; the formulas and the tap table are copied from there).
// The first step also keeps only what is brighter than the threshold, and averages the five overlapping 2x2
// groups of the 13-tap filter with a weight of 1 / (1 + luma): a single very bright pixel (a firefly) can't
// flicker through the whole chain.

#define GROUP_SIZE WI_BLOOM_GROUP_SIZE

// Mirrors DownConstants in bloom_pass.h.
struct BloomDownData
{
	float4 Params; // x = threshold, y = knee, z = 1 for the first step
	float4 Texel;  // xy = 1 / source size (pixels)
};

PUSH_CONSTANTS(BloomDownData, Constants, b0);

Texture2D uSource : register(t1);
IMAGE_FORMAT("rgba16f") RWTexture2D<float4> uOutput : register(u2);
SamplerState uSampler : register(s3);

float Luma(float3 color)
{
	return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// bloom_soft_knee_weight in bloom_math.h
float SoftKneeWeight(float luma, float threshold, float knee)
{
	float soft = clamp(luma - threshold + knee, 0.0, 2.0 * knee);
	float soft_contribution = soft * soft / (4.0 * knee + 1e-5);
	float contribution = max(soft_contribution, luma - threshold);
	return max(contribution, 0.0) / max(luma, 1e-4);
}

// The scene is HDR and may hold NaN or infinity (a bad material, a division): keep that out of the chain.
float3 Sanitize(float3 color)
{
	if (any(isnan(color)) || any(isinf(color)))
	{
		return float3(0.0, 0.0, 0.0);
	}
	return clamp(color, 0.0, 65000.0);
}

// One tap of the first step: the color that is above the threshold.
float3 FirstStepTap(float2 uv)
{
	float3 color = Sanitize(uSource.SampleLevel(uSampler, uv, 0).rgb);
	return color * SoftKneeWeight(Luma(color), Constants.Params.x, Constants.Params.y);
}

float3 KarisAverage(float3 a, float3 b, float3 c, float3 d)
{
	float3 average = (a + b + c + d) * 0.25;
	return average / (1.0 + Luma(average));
}

[numthreads(GROUP_SIZE, GROUP_SIZE, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID)
{
	uint width;
	uint height;
	uOutput.GetDimensions(width, height);
	if (dispatch_id.x >= width || dispatch_id.y >= height)
	{
		return;
	}

	float2 uv = (float2(dispatch_id.xy) + 0.5) / float2(width, height);
	float2 texel = Constants.Texel.xy;

	// The 13 taps of bloom_down_taps(), named by their offset in source texels: m = -, p = +.
	float2 uv_00 = uv;
	float2 uv_mm = uv + float2(-1.0, -1.0) * texel;
	float2 uv_pm = uv + float2(1.0, -1.0) * texel;
	float2 uv_mp = uv + float2(-1.0, 1.0) * texel;
	float2 uv_pp = uv + float2(1.0, 1.0) * texel;
	float2 uv_Mm = uv + float2(-2.0, -2.0) * texel;
	float2 uv_Pm = uv + float2(2.0, -2.0) * texel;
	float2 uv_Mp = uv + float2(-2.0, 2.0) * texel;
	float2 uv_Pp = uv + float2(2.0, 2.0) * texel;
	float2 uv_M0 = uv + float2(-2.0, 0.0) * texel;
	float2 uv_P0 = uv + float2(2.0, 0.0) * texel;
	float2 uv_0M = uv + float2(0.0, -2.0) * texel;
	float2 uv_0P = uv + float2(0.0, 2.0) * texel;

	float3 result;
	if (Constants.Params.z > 0.5)
	{
		float3 c00 = FirstStepTap(uv_00);
		float3 cmm = FirstStepTap(uv_mm);
		float3 cpm = FirstStepTap(uv_pm);
		float3 cmp = FirstStepTap(uv_mp);
		float3 cpp = FirstStepTap(uv_pp);
		float3 cMm = FirstStepTap(uv_Mm);
		float3 cPm = FirstStepTap(uv_Pm);
		float3 cMp = FirstStepTap(uv_Mp);
		float3 cPp = FirstStepTap(uv_Pp);
		float3 cM0 = FirstStepTap(uv_M0);
		float3 cP0 = FirstStepTap(uv_P0);
		float3 c0M = FirstStepTap(uv_0M);
		float3 c0P = FirstStepTap(uv_0P);

		// The five groups of four taps (weights 0.5 for the centre group, 0.125 for each corner group).
		float3 group_centre = (cmm + cpm + cmp + cpp) * 0.25;
		float3 group_tl = (cMm + c0M + cM0 + c00) * 0.25;
		float3 group_tr = (cPm + c0M + cP0 + c00) * 0.25;
		float3 group_bl = (cMp + c0P + cM0 + c00) * 0.25;
		float3 group_br = (cPp + c0P + cP0 + c00) * 0.25;

		float weight_centre = 0.5 / (1.0 + Luma(group_centre));
		float weight_tl = 0.125 / (1.0 + Luma(group_tl));
		float weight_tr = 0.125 / (1.0 + Luma(group_tr));
		float weight_bl = 0.125 / (1.0 + Luma(group_bl));
		float weight_br = 0.125 / (1.0 + Luma(group_br));

		result = (group_centre * weight_centre + group_tl * weight_tl + group_tr * weight_tr + group_bl * weight_bl + group_br * weight_br)
			/ (weight_centre + weight_tl + weight_tr + weight_bl + weight_br);
	}
	else
	{
		result = uSource.SampleLevel(uSampler, uv_00, 0).rgb * 0.125;
		result += (uSource.SampleLevel(uSampler, uv_mm, 0).rgb + uSource.SampleLevel(uSampler, uv_pm, 0).rgb
			+ uSource.SampleLevel(uSampler, uv_mp, 0).rgb + uSource.SampleLevel(uSampler, uv_pp, 0).rgb) * 0.125;
		result += (uSource.SampleLevel(uSampler, uv_Mm, 0).rgb + uSource.SampleLevel(uSampler, uv_Pm, 0).rgb
			+ uSource.SampleLevel(uSampler, uv_Mp, 0).rgb + uSource.SampleLevel(uSampler, uv_Pp, 0).rgb) * 0.03125;
		result += (uSource.SampleLevel(uSampler, uv_M0, 0).rgb + uSource.SampleLevel(uSampler, uv_P0, 0).rgb
			+ uSource.SampleLevel(uSampler, uv_0M, 0).rgb + uSource.SampleLevel(uSampler, uv_0P, 0).rgb) * 0.0625;
	}

	uOutput[dispatch_id.xy] = float4(result, 1.0);
}
