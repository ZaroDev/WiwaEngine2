#include "atmosphere.hlsli"

// The procedural sky behind the scene, evaluated per pixel every frame (so the sun moves smoothly): scattering of the sun in the air,
// the ground below the horizon, and the sun's disc. A fullscreen triangle at the far plane, like sky.hlsl.

struct AtmosphereVertexOutput
{
	float4 Position : SV_Position;
	float2 Ndc : TEXCOORD0;
};

AtmosphereVertexOutput VSMain(uint id : SV_VertexID)
{
	float2 uv = float2((id << 1) & 2, id & 2);
	AtmosphereVertexOutput output;
	output.Ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
	output.Position = float4(output.Ndc, 1.0, 1.0);
	return output;
}

float4 PSMain(AtmosphereVertexOutput input) : SV_Target0
{
	float4 far = mul(InverseViewProjection, float4(input.Ndc, 1.0, 1.0));
	float3 direction = normalize(far.xyz / far.w - CameraPosition.xyz);
	float3 radiance = SkyRadiance(direction) * AtmosphereIrradiance.w + SunDisc(direction);
	return float4(radiance, 1.0);
}
