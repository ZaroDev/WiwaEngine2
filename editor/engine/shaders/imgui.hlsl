#include "Common.hlsli"

// Slots match ImGuiPass: SRV 0, sampler 1, push constants 2.
struct PushConstantsData
{
	float2 Scale;
	float2 Translate;
};

PUSH_CONSTANTS(PushConstantsData, Constants, b2);

Texture2D uTexture : register(t0);
SamplerState uSampler : register(s1);

struct VertexInput
{
	float2 Position : POSITION;
	float2 Uv : TEXCOORD;
	float4 Color : COLOR;
};

struct VertexOutput
{
	float4 Position : SV_Position;
	float2 Uv : TEXCOORD0;
	float4 Color : COLOR0;
};

VertexOutput VSMain(VertexInput input)
{
	VertexOutput output;
	output.Position = float4(input.Position * Constants.Scale + Constants.Translate, 0.0, 1.0);
	output.Uv = input.Uv;
	output.Color = input.Color;
	return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
	return input.Color * uTexture.Sample(uSampler, input.Uv);
}
