#define SKIN_SET 1 // the sets before it in this pass: see scene_common.hlsli
#include "depth.hlsli"

// The depth prepass for opaque materials: depth, the normal and the velocity in the color targets.
DepthVertexOutput VSMain(MeshVertexInput input)
{
	return DepthVertex(input);
}

DepthOutput PSMain(DepthVertexOutput input, bool frontFace : SV_IsFrontFace)
{
	return DepthPixel(input, frontFace);
}
