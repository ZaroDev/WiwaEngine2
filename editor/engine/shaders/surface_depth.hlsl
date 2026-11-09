#define SKIN_SET 4 // the sets before it in this pass: see scene_common.hlsli
#include "depth.hlsli"
#include "surface_contract.hlsli"

// The depth prepass of a material with a surface shader: depth, the normal and the velocity, at the position VertexOffset moved
// the vertex to (in this frame and the previous one alike). With SURFACE_MASKED (the DepthMasked variant) Surface runs for the
// alpha, and pixels under the material's cutoff write nothing.

DepthVertexOutput VSMain(MeshVertexInput input)
{
	return DepthVertexAt(input, SurfaceObjectPosition(input, DrawConstants.InstanceIndex, DrawConstants.MaterialSlot));
}

DepthOutput PSMain(DepthVertexOutput input, bool frontFace : SV_IsFrontFace)
{
#ifdef SURFACE_MASKED
	MaterialRecord material = uMaterials[DrawConstants.MaterialSlot];
	SurfaceInput surfaceInput = MakeSurfaceInput(input.WorldPosition, input.Normal, input.Uv, frontFace);
	SurfaceOutput surfaceOutput = EvaluateSurface(material, DrawConstants.MaterialSlot, surfaceInput);
	if (surfaceOutput.Alpha < material.AlphaCutoff)
	{
		discard;
	}
#endif
	return DepthPixel(input, frontFace);
}
