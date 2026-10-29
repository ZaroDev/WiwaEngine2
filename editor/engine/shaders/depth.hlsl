#include "depth.hlsli"

// The depth prepass for opaque materials: depth, and the normal in the color target.
DepthVertexOutput VSMain(MeshVertexInput input)
{
	return DepthVertex(input);
}

float2 PSMain(DepthVertexOutput input, bool frontFace : SV_IsFrontFace) : SV_Target0
{
	return DepthNormal(input, frontFace);
}
