#define SKIN_SET 4 // the sets before it in this pass: see scene_common.hlsli
#include "surface_contract.hlsli"

// Draws a shadow caster with a surface shader into a shadow map: the position VertexOffset moved the vertex to and, with
// SURFACE_MASKED (the ShadowMasked variant), the alpha cutout of Surface. The built-in shadow.hlsl puts the shadow matrix index
// in the push constants' MaterialSlot; a caster with a surface shader also needs its material, so here the low 3 bits are the
// matrix (0..3 the sun's cascades, 4..7 the spot lights) and the rest the material slot: MaterialSlot = matrix | (slot << 3).

uint SurfaceShadowMatrix()
{
	return DrawConstants.MaterialSlot & 7u;
}

uint SurfaceShadowMaterial()
{
	return DrawConstants.MaterialSlot >> 3u;
}

struct SurfaceShadowVertexOutput
{
	float4 Position : SV_Position;
	float3 WorldPosition : TEXCOORD0;
	float3 WorldNormal : TEXCOORD1;
	float2 Uv : TEXCOORD2;
};

SurfaceShadowVertexOutput VSMain(MeshVertexInput input)
{
	SceneInstance instance = uInstances[DrawConstants.InstanceIndex];
	float3 worldPosition = TransformPoint(instance, SkinPoint(instance, input, SurfaceObjectPosition(input, DrawConstants.InstanceIndex, SurfaceShadowMaterial())));

	SurfaceShadowVertexOutput output;
	output.Position = mul(ShadowMatrices[SurfaceShadowMatrix()], float4(worldPosition, 1.0));
	output.WorldPosition = worldPosition;
	output.WorldNormal = TransformNormal(instance, SkinNormal(instance, input, input.Normal));
	output.Uv = input.Uv;
	return output;
}

void PSMain(SurfaceShadowVertexOutput input, bool frontFace : SV_IsFrontFace)
{
#ifdef SURFACE_MASKED
	uint materialSlot = SurfaceShadowMaterial();
	MaterialRecord material = uMaterials[materialSlot];
	SurfaceInput surfaceInput = MakeSurfaceInput(input.WorldPosition, input.WorldNormal, input.Uv, frontFace);
	SurfaceOutput surfaceOutput = EvaluateSurface(material, materialSlot, surfaceInput);
	if (surfaceOutput.Alpha < material.AlphaCutoff)
	{
		discard;
	}
#endif
}
