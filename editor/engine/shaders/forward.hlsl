#define SKIN_SET 3 // the sets before it in this pass: see scene_common.hlsli
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
	float3 worldPosition = TransformPoint(instance, SkinPoint(instance, input, input.Position));

	ForwardVertexOutput output;
	output.Position = mul(ViewProjection, float4(worldPosition, 1.0));
	output.WorldPosition = worldPosition;
	output.WorldNormal = TransformNormal(instance, SkinNormal(instance, input, input.Normal));
	output.Uv = input.Uv;
	return output;
}

float4 PSMain(ForwardVertexOutput input, bool frontFace : SV_IsFrontFace) : SV_Target0
{
	SurfaceData surface = LoadSurface(input.WorldPosition, input.WorldNormal, input.Uv, frontFace);

	// The depth prepass left no depth where a masked material is cut out, so these pixels must not be shaded here either.
	MaterialRecord material = uMaterials[DrawConstants.MaterialSlot];
	if ((material.Flags & MATERIAL_ALPHA_MASK_BIT) != 0u && surface.Alpha < material.AlphaCutoff)
	{
		discard;
	}

	float3 color = ForwardLighting(surface, input.Position.xy);
	return float4(color, surface.Alpha);
}
