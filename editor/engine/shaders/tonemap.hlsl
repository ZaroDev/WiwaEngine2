#include "Common.hlsli"

// Turns the HDR scene into the 8-bit viewport image: exposure, color grading, the tone mapping operator, a vignette, then the sRGB
// encoding. Slots match TonemapPass: SRV 0, sampler 1, push constants 2.
struct TonemapConstantsData
{
	float Exposure;
	uint Operator; // 0 ACES, 1 Reinhard, 2 Neutral, 3 None
	float Contrast;
	float Saturation;
	float Temperature;
	float Tint;
	float Vignette;
	float AutoExposure; // 1: the exposure is in uExposure[0]
	float BloomIntensity; // 0: the bloom is not read
	float3 Padding;
};

PUSH_CONSTANTS(TonemapConstantsData, Constants, b2);

Texture2D uHdr : register(t0);
SamplerState uSampler : register(s1);
StructuredBuffer<float> uExposure : register(t3); // written by the exposure pass
Texture2D uBloom : register(t4);
SamplerState uLinearSampler : register(s5);

struct VertexOutput
{
	float4 Position : SV_Position;
	float2 Uv : TEXCOORD0;
};

// One triangle that covers the screen.
VertexOutput VSMain(uint id : SV_VertexID)
{
	float2 uv = float2((id << 1) & 2, id & 2);
	VertexOutput output;
	output.Position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
	output.Uv = uv;
	return output;
}

float3 AcesFilm(float3 x)
{
	return saturate((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14));
}

float3 LinearToSrgb(float3 c)
{
	return lerp(c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055, step(0.0031308, c));
}

// Khronos PBR Neutral: keeps hue and saturation of everything below the shoulder.
float3 NeutralTonemap(float3 color)
{
	const float startCompression = 0.8 - 0.04;
	const float desaturation = 0.15;
	float x = min(color.r, min(color.g, color.b));
	float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
	color -= offset;
	float peak = max(color.r, max(color.g, color.b));
	if (peak < startCompression)
	{
		return color;
	}
	const float d = 1.0 - startCompression;
	float newPeak = 1.0 - d * d / (peak + d - startCompression);
	color *= newPeak / peak;
	float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
	return lerp(color, newPeak * float3(1.0, 1.0, 1.0), g);
}

float3 ApplyOperator(float3 color, uint op)
{
	if (op == 0)
	{
		return AcesFilm(color);
	}
	if (op == 1)
	{
		return color / (1.0 + color);
	}
	if (op == 2)
	{
		return saturate(NeutralTonemap(color));
	}
	return saturate(color);
}

// White balance by temperature (blue-orange) and tint (green-magenta), contrast about middle gray, saturation about luminance.
float3 Grade(float3 color)
{
	color *= float3(1.0 + 0.3 * Constants.Temperature, 1.0 - 0.3 * Constants.Tint, 1.0 - 0.3 * Constants.Temperature);
	color = pow(max(color, 0.0) / 0.18, Constants.Contrast) * 0.18;
	float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
	return max(lerp(float3(luma, luma, luma), color, Constants.Saturation), 0.0);
}

float4 PSMain(VertexOutput input) : SV_Target0
{
	float exposure = Constants.Exposure;
	if (Constants.AutoExposure > 0.5)
	{
		float measured = uExposure[0];
		if (measured > 0.0 && measured < 1e6) // not yet measured (0) or broken (NaN, huge): the manual exposure stays
		{
			exposure = measured;
		}
	}
	float3 hdr = uHdr.SampleLevel(uSampler, input.Uv, 0).rgb;
	if (Constants.BloomIntensity > 0.0) // a uniform branch: nothing is read when it is off
	{
		hdr += uBloom.SampleLevel(uLinearSampler, input.Uv, 0).rgb * Constants.BloomIntensity;
	}
	hdr *= exposure;
	float3 mapped = ApplyOperator(Grade(hdr), Constants.Operator);
	float2 fromCenter = input.Uv - 0.5;
	mapped *= 1.0 - Constants.Vignette * smoothstep(0.25, 0.75, length(fromCenter));
	return float4(LinearToSrgb(mapped), 1.0);
}
