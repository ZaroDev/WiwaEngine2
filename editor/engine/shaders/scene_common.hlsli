#ifndef SCENE_COMMON_HLSLI
#define SCENE_COMMON_HLSLI

#include "Common.hlsli"

// What every scene pass sees (set 0 on Vulkan; registers are unique across layouts because shader model 5.0 has no spaces):
//   b0  FrameBuffer      per frame: camera, sun, ambient, viewport
//   b1  push constants   per draw: which instance, which material
//   t1  uInstances       one record per scene instance
//   t2  uMaterials       one record per material slot
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
