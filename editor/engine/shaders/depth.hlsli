#ifndef DEPTH_HLSLI
#define DEPTH_HLSLI

#include "scene_common.hlsli"

struct DepthVertexOutput
{
	float4 Position : SV_Position;
	float2 Uv : TEXCOORD0;
	float3 Normal : TEXCOORD1;
};

// What the prepass writes besides depth: the world normal, facing the viewer, octahedral encoded.
float2 DepthNormal(DepthVertexOutput input, bool frontFace)
{
	return EncodeOctahedral(frontFace ? input.Normal : -input.Normal);
}

DepthVertexOutput DepthVertex(MeshVertexInput input)
{
	SceneInstance instance = uInstances[DrawConstants.InstanceIndex];
	DepthVertexOutput output;
	output.Position = mul(ViewProjection, float4(TransformPoint(instance, input.Position), 1.0));
	output.Uv = input.Uv;
	output.Normal = TransformNormal(instance, input.Normal);
	return output;
}

#endif // DEPTH_HLSLI
