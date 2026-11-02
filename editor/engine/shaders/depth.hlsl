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
