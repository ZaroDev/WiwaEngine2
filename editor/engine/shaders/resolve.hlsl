#include "Common.hlsli"

// Turns the HDR scene into the 8-bit viewport image: exposure, the ACES filmic curve, then the sRGB encoding.
// Slots match ResolvePass: SRV 0, sampler 1, push constants 2.
struct ResolveConstantsData
{
	float Exposure;
	float3 Padding;
};

PUSH_CONSTANTS(ResolveConstantsData, Constants, b2);

Texture2D uHdr : register(t0);
SamplerState uSampler : register(s1);

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

float4 PSMain(VertexOutput input) : SV_Target0
{
	float3 hdr = uHdr.SampleLevel(uSampler, input.Uv, 0).rgb * Constants.Exposure;
	return float4(LinearToSrgb(AcesFilm(hdr)), 1.0);
}
