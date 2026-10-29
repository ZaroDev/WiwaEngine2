#include "Common.hlsli"

// Draws the finished scene image over the whole window (a game has no panel to show it in). The image is already sRGB encoded and
// the swapchain is not, so it is copied as it is. Slots match BlitPass: SRV 0, sampler 1.
Texture2D uImage : register(t0);
SamplerState uSampler : register(s1);

struct VertexOutput
{
	float4 Position : SV_Position;
	float2 Uv : TEXCOORD0;
};

VertexOutput VSMain(uint id : SV_VertexID)
{
	float2 uv = float2((id << 1) & 2, id & 2);
	VertexOutput output;
	output.Position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
	output.Uv = uv;
	return output;
}

float4 PSMain(VertexOutput input) : SV_Target0
{
	return float4(uImage.SampleLevel(uSampler, input.Uv, 0).rgb, 1.0);
}
