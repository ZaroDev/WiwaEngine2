#ifndef DEPTH_HLSLI
#define DEPTH_HLSLI

#include "scene_common.hlsli"

struct DepthVertexOutput
{
	float4 Position : SV_Position;
	float2 Uv : TEXCOORD0;
	float3 Normal : TEXCOORD1;
	float4 CurrentClip : TEXCOORD2;  // clip position without the TAA jitter
	float4 PreviousClip : TEXCOORD3; // where the same point of the surface was in the previous frame
	float3 WorldPosition : TEXCOORD4; // for the surface shaders of masked materials (surface_depth.hlsl)
};

// What the prepass writes besides depth: the world normal, facing the viewer, octahedral encoded, and how far the pixel moved.
struct DepthOutput
{
	float2 Normal : SV_Target0;
	float2 Velocity : SV_Target1;
};

// Where the point of the surface was in the previous frame, in that frame's clip space. A rigid instance moved with its world matrix;
// a skinned mesh has to skin the point with the previous frame's pose here instead, so that the velocity includes the animation.
float4 PreviousClipPosition(MeshVertexInput input, float3 position, SceneInstance instance)
{
	position = SkinPointWith(instance, input, position, true);
	float3 world = instance.PrevWorldC0.xyz * position.x + instance.PrevWorldC1.xyz * position.y + instance.PrevWorldC2.xyz * position.z + instance.PrevWorldC3.xyz;
	return mul(PreviousViewProjection, float4(world, 1.0));
}

float2 DepthNormal(DepthVertexOutput input, bool frontFace)
{
	return OctahedralEncode(frontFace ? input.Normal : -input.Normal);
}

// Current minus previous position as a uv delta (uv has its origin at the top left, clip-space Y points up).
float2 DepthVelocity(DepthVertexOutput input)
{
	if (input.PreviousClip.w <= 1e-4 || input.CurrentClip.w <= 1e-4)
	{
		return float2(0.0, 0.0); // the point was behind the camera: no usable motion
	}
	float2 current = input.CurrentClip.xy / input.CurrentClip.w;
	float2 previous = input.PreviousClip.xy / input.PreviousClip.w;
	return (current - previous) * float2(0.5, -0.5);
}

DepthOutput DepthPixel(DepthVertexOutput input, bool frontFace)
{
	DepthOutput output;
	output.Normal = DepthNormal(input, frontFace);
	output.Velocity = DepthVelocity(input);
	return output;
}

// The vertex at an object-space position (the mesh's, or moved by a surface shader's VertexOffset): this frame and the previous
// one both use it, so an offset that does not change over time gives the velocity of the mesh itself.
DepthVertexOutput DepthVertexAt(MeshVertexInput input, float3 position)
{
	SceneInstance instance = uInstances[DrawConstants.InstanceIndex];
	float3 world = TransformPoint(instance, SkinPoint(instance, input, position));
	DepthVertexOutput output;
	output.Position = mul(ViewProjection, float4(world, 1.0));
	output.Uv = input.Uv;
	output.Normal = TransformNormal(instance, SkinNormal(instance, input, input.Normal));
	output.CurrentClip = mul(UnjitteredViewProjection, float4(world, 1.0));
	output.PreviousClip = PreviousClipPosition(input, position, instance);
	output.WorldPosition = world;
	return output;
}

DepthVertexOutput DepthVertex(MeshVertexInput input)
{
	return DepthVertexAt(input, input.Position);
}

#endif // DEPTH_HLSLI
