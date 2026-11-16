#define SKIN_SET 4 // the sets before it in this pass: see scene_common.hlsli
#include "surface_contract.hlsli"

// The forward pass of a material with a surface shader (see surface_contract.hlsli): the same lighting as forward.hlsl, on the
// surface the shader returns. Masked pixels under the cutoff are discarded, as the prepass cut them out of the depth.

struct SurfaceForwardVertexOutput
{
	float4 Position : SV_Position;
	float3 WorldPosition : TEXCOORD0;
	float3 WorldNormal : TEXCOORD1;
	float2 Uv : TEXCOORD2;
};

SurfaceForwardVertexOutput VSMain(MeshVertexInput input)
{
	SceneInstance instance = uInstances[DrawConstants.InstanceIndex];
	float3 worldPosition = TransformPoint(instance, SkinPoint(instance, input, SurfaceObjectPosition(input, DrawConstants.InstanceIndex, DrawConstants.MaterialSlot)));

	SurfaceForwardVertexOutput output;
	output.Position = mul(ViewProjection, float4(worldPosition, 1.0));
	output.WorldPosition = worldPosition;
	output.WorldNormal = TransformNormal(instance, SkinNormal(instance, input, input.Normal));
	output.Uv = input.Uv;
	return output;
}

float4 PSMain(SurfaceForwardVertexOutput input, bool frontFace : SV_IsFrontFace) : SV_Target0
{
	MaterialRecord material = uMaterials[DrawConstants.MaterialSlot];
	SurfaceInput surfaceInput = MakeSurfaceInput(input.WorldPosition, input.WorldNormal, input.Uv, frontFace);
	SurfaceOutput surfaceOutput = EvaluateSurface(material, DrawConstants.MaterialSlot, surfaceInput);
	SurfaceData surface = SurfaceShadingData(surfaceInput, surfaceOutput, input.Position.xy, (material.Flags & MATERIAL_ALPHA_BLEND_BIT) == 0u);

	if ((material.Flags & MATERIAL_ALPHA_MASK_BIT) != 0u && surface.Alpha < material.AlphaCutoff)
	{
		discard;
	}

	// The alpha of the HDR image is coverage: opaque and masked surfaces cover the pixel.
	const float alpha = (material.Flags & MATERIAL_ALPHA_BLEND_BIT) != 0u ? surface.Alpha : 1.0;
	return float4(ForwardLighting(surface, input.Position.xy), alpha);
}
