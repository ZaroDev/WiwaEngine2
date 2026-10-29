#include "depth.hlsli"

// The depth prepass for opaque materials: positions only, no pixel stage.
DepthVertexOutput VSMain(MeshVertexInput input)
{
	return DepthVertex(input);
}
