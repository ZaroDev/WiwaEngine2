#include "scene_common.hlsli"

// The sky behind the scene: the environment cube (mip 0) seen along the direction of each pixel, turned like the lighting is.
// A fullscreen triangle at the far plane; the depth test leaves it where nothing was drawn.

struct SkyVertexOutput
{
	float4 Position : SV_Position;
	float2 Ndc : TEXCOORD0;
};

SkyVertexOutput VSMain(uint id : SV_VertexID)
{
	float2 uv = float2((id << 1) & 2, id & 2);
	SkyVertexOutput output;
	output.Ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
	output.Position = float4(output.Ndc, 1.0, 1.0);
	return output;
}

float4 PSMain(SkyVertexOutput input) : SV_Target0
{
	float4 far = mul(InverseViewProjection, float4(input.Ndc, 1.0, 1.0));
	float3 direction = normalize(far.xyz / far.w - CameraPosition.xyz);
	float3 radiance = uEnvironment.SampleLevel(uEnvironmentSampler, RotateEnvironment(direction), 0).rgb * EnvRotation.z;
	return float4(radiance, 1.0);
}
