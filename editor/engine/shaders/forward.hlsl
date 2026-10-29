#include "pbr.hlsli"

// The forward pass: PBR metallic-roughness for opaque and alpha-masked materials (the depth prepass has already cut the masked
// pixels out), and the same shading for blended ones.

struct ForwardVertexOutput
{
	float4 Position : SV_Position;
	float3 WorldPosition : TEXCOORD0;
	float3 WorldNormal : TEXCOORD1;
	float2 Uv : TEXCOORD2;
};

ForwardVertexOutput VSMain(MeshVertexInput input)
{
	SceneInstance instance = uInstances[DrawConstants.InstanceIndex];
	float3 worldPosition = TransformPoint(instance, input.Position);

	ForwardVertexOutput output;
	output.Position = mul(ViewProjection, float4(worldPosition, 1.0));
	output.WorldPosition = worldPosition;
	output.WorldNormal = TransformNormal(instance, input.Normal);
	output.Uv = input.Uv;
	return output;
}

float4 PSMain(ForwardVertexOutput input, bool frontFace : SV_IsFrontFace) : SV_Target0
{
	SurfaceData surface = LoadSurface(input.WorldPosition, input.WorldNormal, input.Uv, frontFace);

	float3 color = AmbientLight(surface) + surface.Emissive;
	if (SunDirection.w > 0.5)
	{
		color += DirectLight(surface, -SunDirection.xyz, SunColor.rgb);
	}
	color += ClusteredLights(surface, input.Position.xy);
	return float4(color, surface.Alpha);
}
