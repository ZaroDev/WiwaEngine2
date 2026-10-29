#ifndef SCENE_COMMON_HLSLI
#define SCENE_COMMON_HLSLI

#include "Common.hlsli"

// What every scene pass sees (set 0 on Vulkan; registers are unique across layouts because shader model 5.0 has no spaces):
//   b0  FrameBuffer      per frame: camera, sun, ambient, viewport
//   b1  push constants   per draw: which instance, which material
//   t1  uInstances       one record per scene instance
//   t2  uMaterials       one record per material slot
//   t3  uLights          the point and spot lights
//   t4  uClusterCounts   per cluster: how many lights reach it      (written by light_cull.hlsl as u6)
//   t5  uClusterIndices  per cluster: MAX_LIGHTS_PER_CLUSTER light indices (written by light_cull.hlsl as u7)
//   t6  uShadowCascades  the sun's shadow maps (a depth array)      s14 uShadowSampler (comparison)
//   t7  uSpotAtlas       the spot lights' shadow maps (one atlas)
// Materials' textures are in set 1 (see material_common.hlsli). Mirrors FrameConstants / GpuInstance / GpuMaterialRecord in C++.

static const float PI = 3.14159265359;

static const uint MATERIAL_ALPHA_MASK_BIT = 1u;
static const uint MATERIAL_ALPHA_BLEND_BIT = 2u;
static const uint MATERIAL_DOUBLE_SIDED_BIT = 4u;
static const uint MATERIAL_HAS_BASE_COLOR = 1u;
static const uint MATERIAL_HAS_NORMAL = 2u;
static const uint MATERIAL_HAS_ORM = 4u;
static const uint MATERIAL_HAS_EMISSIVE = 8u;

cbuffer FrameBuffer : register(b0)
{
	float4x4 View;
	float4x4 Projection;
	float4x4 ViewProjection;
	float4 CameraPosition;  // xyz, near
	float4 CameraParams;    // x = far
	float4 SunDirection;    // xyz = the way the light travels, w = 1 when there is a sun
	float4 SunColor;        // rgb * intensity
	float4 SkyColor;        // rgb * ambient intensity
	float4 GroundColor;
	float4 ViewportSize;    // width, height, 1 / width, 1 / height
	float4 ClusterParams;   // x = slice scale, y = slice bias (slice = log2(viewDepth) * x + y), z = 1 for an orthographic camera, w = 1 to skip the clusters (debug)
	uint4 LightInfo;        // x = number of lights in uLights
	float4x4 ShadowMatrices[8]; // 0..3 the sun's cascades, 4..7 the shadowed spot lights
	float4 CascadeSplits;   // view depth at which each cascade ends
	float4 CascadeTexels;   // world size of a texel of each cascade
	float4 ShadowParams;    // x = 1 when the sun casts shadows, y = cascade count, z = depth bias, w = normal bias (texels)
	float4 ShadowParams2;   // x = spot light depth bias
	float4 SpotShadowRects[4]; // the light's tile in the atlas: offset xy, scale zw
	float4 SpotShadowInfo[4];  // x = tan(half the cone angle)
};

// The cluster grid the lights are sorted into: screen tiles times exponential depth slices. Mirrors the constants in C++.
static const uint CLUSTERS_X = 16;
static const uint CLUSTERS_Y = 9;
static const uint CLUSTERS_Z = 24;
static const uint MAX_LIGHTS_PER_CLUSTER = 64;
static const float CASCADE_MAP_SIZE = 2048.0;
static const float SPOT_TILE_SIZE = 1024.0;
static const float SPOT_ATLAS_SIZE = 4096.0;

static const uint LIGHT_POINT = 0;
static const uint LIGHT_SPOT = 1;

struct GpuLight
{
	float3 Position;
	float Range;
	float3 Color;
	float Intensity;
	float3 Direction;  // the way a spot light points
	float InnerCos;
	float OuterCos;
	uint Type;
	uint ShadowIndex;
	uint Padding;
};

struct SceneInstance
{
	float4 WorldC0;
	float4 WorldC1;
	float4 WorldC2;
	float4 WorldC3;
	float4 NormalC0;
	float4 NormalC1;
	float4 NormalC2;
	float4 NormalC3;
};

struct MaterialRecord
{
	float4 BaseColor;
	float3 Emissive;
	float Metallic;
	float Roughness;
	float AlphaCutoff;
	uint Flags;
	uint TextureMask;
	float4 Extra; // clearcoat, clearcoat roughness, sheen, transmission: stored, not shaded yet
};

struct DrawConstantsData
{
	uint InstanceIndex;
	uint MaterialSlot;
};

PUSH_CONSTANTS(DrawConstantsData, DrawConstants, b1);

StructuredBuffer<SceneInstance> uInstances : register(t1);
StructuredBuffer<MaterialRecord> uMaterials : register(t2);
StructuredBuffer<GpuLight> uLights : register(t3);
StructuredBuffer<uint> uClusterCounts : register(t4);
StructuredBuffer<uint> uClusterIndices : register(t5);
Texture2DArray<float> uShadowCascades : register(t6);
Texture2D<float> uSpotAtlas : register(t7);
SamplerComparisonState uShadowSampler : register(s14);

// The cluster of a pixel: its screen tile and the depth slice of its view-space distance.
uint ClusterIndexOf(float2 pixel, float viewDepth)
{
	uint tileX = min((uint)(pixel.x / ViewportSize.x * CLUSTERS_X), CLUSTERS_X - 1);
	uint tileY = min((uint)(pixel.y / ViewportSize.y * CLUSTERS_Y), CLUSTERS_Y - 1);
	int slice = (int)floor(log2(max(viewDepth, 1e-4)) * ClusterParams.x + ClusterParams.y);
	slice = clamp(slice, 0, (int)CLUSTERS_Z - 1);
	return (uint)slice * (CLUSTERS_X * CLUSTERS_Y) + tileY * CLUSTERS_X + tileX;
}

float3 TransformPoint(SceneInstance instance, float3 position)
{
	return instance.WorldC0.xyz * position.x + instance.WorldC1.xyz * position.y + instance.WorldC2.xyz * position.z + instance.WorldC3.xyz;
}

float3 TransformNormal(SceneInstance instance, float3 normal)
{
	return normalize(instance.NormalC0.xyz * normal.x + instance.NormalC1.xyz * normal.y + instance.NormalC2.xyz * normal.z);
}

struct MeshVertexInput
{
	float3 Position : POSITION;
	float3 Normal : NORMAL;
	float2 Uv : TEXCOORD;
};

#endif // SCENE_COMMON_HLSLI
