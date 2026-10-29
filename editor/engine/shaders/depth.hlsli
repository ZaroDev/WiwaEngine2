#ifndef DEPTH_HLSLI
#define DEPTH_HLSLI

#include "scene_common.hlsli"

struct DepthVertexOutput
{
	float4 Position : SV_Position;
	float2 Uv : TEXCOORD0;
};

DepthVertexOutput DepthVertex(MeshVertexInput input)
{
	SceneInstance instance = uInstances[DrawConstants.InstanceIndex];
	DepthVertexOutput output;
	output.Position = mul(ViewProjection, float4(TransformPoint(instance, input.Position), 1.0));
	output.Uv = input.Uv;
	return output;
}

#endif // DEPTH_HLSLI
