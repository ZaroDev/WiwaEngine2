#define SKIN_SET 4 // the sets before it in this pass: see scene_common.hlsli
#include "depth.hlsli"
#include "surface_contract.hlsli"

// The velocity of a blended material with a surface shader, drawn after the forward pass like velocity_blend.hlsl: only where the
// surface the shader returns is mostly opaque (alpha 0.5 or more), at the position VertexOffset moved the vertex to.

DepthVertexOutput VSMain(MeshVertexInput input)
{
	return DepthVertexAt(input, SurfaceObjectPosition(input, DrawConstants.InstanceIndex, DrawConstants.MaterialSlot));
}

float2 PSMain(DepthVertexOutput input, bool frontFace : SV_IsFrontFace) : SV_Target0
{
	MaterialRecord material = uMaterials[DrawConstants.MaterialSlot];
	SurfaceInput surfaceInput = MakeSurfaceInput(input.WorldPosition, input.Normal, input.Uv, frontFace);
	SurfaceOutput surfaceOutput = EvaluateSurface(material, DrawConstants.MaterialSlot, surfaceInput);
	if (surfaceOutput.Alpha < 0.5)
	{
		discard;
	}
	return DepthVelocity(input);
}
