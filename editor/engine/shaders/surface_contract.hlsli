// The contract between the engine and a user surface shader (.wshader), version 1.
//
// A .wshader is not compiled on its own. The importer (surface_shader_importer.cpp) builds one source per pass variant:
//
//   #define SURFACE_COMPOSED / SURFACE_MASKED / SURFACE_HAS_VERTEX_OFFSET
//   #include "surface_contract.hlsli"          this file: the types below and the custom-material resources
//   the user file                              MaterialParams, "Texture2D X; // @param" (bound to t16..t19), Surface, VertexOffset
//   LoadMaterialParams(slot)                   generated: reads the material's parameter block from t21
//   #include "surface_<variant>.hlsl"          the entry points: forward, depth, velocity or shadow
//
// The user hooks:
//   void Surface(SurfaceInput i, MaterialParams p, inout SurfaceOutput o);   required; o arrives filled with what the built-in
//                                                                            material computes, so a shader changes only what it wants
//   float3 VertexOffset(VertexInput v, MaterialParams p);                    optional; an object-space offset added to the position in
//                                                                            every pass that draws the material (not picking or outlines);
//                                                                            on a skinned mesh it is applied before the skinning
// Lighting, shadows, AO, clustered lights and velocity stay the engine's. Surface runs in pixel shaders (Sample is fine there);
// VertexOffset runs in vertex shaders, so textures must be read with SampleLevel.
//
// The custom-material layout (set 3 on Vulkan; shader model 5.0 has no spaces, so these registers belong to it and nothing else;
// SM5.0 has only s0..s15, so the sampler takes the free s15 and its Vulkan binding is 15 as well):
//   t16..t19  the @param textures, in file order
//   t20       uSurfaceRecords   per material slot: where its parameter block is in uSurfaceParams
//   t21       uSurfaceParams    every custom material's parameter block (HLSL cbuffer packing, at most 256 bytes)
//   s15       uSurfaceSampler   the sampler of the @param textures
#ifndef SURFACE_CONTRACT_HLSLI
#define SURFACE_CONTRACT_HLSLI

#include "pbr.hlsli"

struct SurfaceInput
{
	float3 WorldPosition;
	float3 WorldNormal;    // unit length, facing the viewer (flipped on back faces)
	float3 WorldTangent;   // the tangent frame of the uv, from screen-space derivatives
	float3 WorldBitangent;
	float2 Uv;
	float3 ViewDirection;  // unit vector from the surface to the camera
	bool FrontFace;
	float Time;            // seconds
};

struct SurfaceOutput
{
	float3 Albedo;
	float3 NormalTS;       // tangent space; (0, 0, 1) is the geometric normal
	float Metallic;
	float Roughness;
	float3 Emissive;
	float Alpha;
	float Occlusion;
};

struct VertexInput
{
	float3 Position;       // object space
	float3 Normal;
	float2 Uv;
	uint InstanceIndex;    // into uInstances
};

// Mirrors GpuCustomRecord in C++.
struct SurfaceRecord
{
	uint ParamOffset;      // bytes into uSurfaceParams, a multiple of 16
	uint ParamSize;
	uint2 Reserved;
};

VK_BINDING(20, 3) StructuredBuffer<SurfaceRecord> uSurfaceRecords : register(t20);
VK_BINDING(21, 3) ByteAddressBuffer uSurfaceParams : register(t21);
VK_BINDING(15, 3) SamplerState uSurfaceSampler : register(s15);

// The time a surface shader sees (SurfaceInput.Time). The frame constants carry no clock yet, so it is always 0 for now.
float SurfaceTime()
{
	return 0.0;
}

// Where a material's parameter block starts in uSurfaceParams.
uint SurfaceParamOffset(uint materialSlot)
{
	return uSurfaceRecords[materialSlot].ParamOffset;
}

// The SurfaceOutput a shader starts from: the built-in material's own values at this uv (pbr.hlsli).
SurfaceOutput BuiltinSurfaceOutput(MaterialRecord material, float2 uv)
{
	BuiltinSurface builtin = EvaluateBuiltinSurface(material, uv);
	SurfaceOutput output;
	output.Albedo = builtin.Albedo;
	output.NormalTS = builtin.NormalTS;
	output.Metallic = builtin.Metallic;
	output.Roughness = builtin.Roughness;
	output.Emissive = builtin.Emissive;
	output.Alpha = builtin.Alpha;
	output.Occlusion = builtin.Occlusion;
	return output;
}

// Pixel shaders only (the tangent frame uses derivatives).
SurfaceInput MakeSurfaceInput(float3 worldPosition, float3 worldNormal, float2 uv, bool frontFace)
{
	float3 normal = normalize(worldNormal);
	if (!frontFace)
	{
		normal = -normal;
	}

	SurfaceInput input;
	input.WorldPosition = worldPosition;
	input.WorldNormal = normal;
	SurfaceTangentFrame(worldPosition, normal, uv, input.WorldTangent, input.WorldBitangent);
	input.Uv = uv;
	input.ViewDirection = normalize(CameraPosition.xyz - worldPosition);
	input.FrontFace = frontFace;
	input.Time = SurfaceTime();
	return input;
}

VertexInput MakeVertexInput(MeshVertexInput input, uint instanceIndex)
{
	VertexInput vertex;
	vertex.Position = input.Position;
	vertex.Normal = input.Normal;
	vertex.Uv = input.Uv;
	vertex.InstanceIndex = instanceIndex;
	return vertex;
}

// What the lighting takes from the shader's output: the tangent-space normal brought into world space, and the decals of the pixel
// (pixel = SV_Position.xy) laid over it unless the material is blended.
SurfaceData SurfaceShadingData(SurfaceInput input, SurfaceOutput output, float2 pixel, bool receivesDecals)
{
	float3 normal = normalize(mul(output.NormalTS, float3x3(input.WorldTangent, input.WorldBitangent, input.WorldNormal)));
	float3 albedo = output.Albedo;
	float metallic = output.Metallic;
	float roughness = output.Roughness;
	float occlusion = output.Occlusion;
	float3 emissive = output.Emissive;
	if (receivesDecals)
	{
		ApplyDecals(pixel, albedo, normal, metallic, roughness, occlusion, emissive);
	}
	return MakeSurfaceData(input.WorldPosition, normal, albedo, metallic, roughness, occlusion, emissive, output.Alpha);
}

#ifndef SURFACE_COMPOSED
// An entry template compiled on its own (the engine cooks every .hlsl file): the default surface, which changes nothing.
struct MaterialParams
{
	int SurfaceUnused;
};

MaterialParams LoadMaterialParams(uint materialSlot)
{
	return (MaterialParams)0;
}

void Surface(SurfaceInput i, MaterialParams p, inout SurfaceOutput o)
{
}

#define SURFACE_HOOKS_READY 1
#endif

#endif // SURFACE_CONTRACT_HLSLI

// The part of the contract that calls the user's hooks: available once MaterialParams, LoadMaterialParams and the hooks exist (the
// importer defines SURFACE_HOOKS_READY after the user file; the entry templates include this file again).
#if defined(SURFACE_HOOKS_READY) && !defined(SURFACE_CONTRACT_HOOKS)
#define SURFACE_CONTRACT_HOOKS

// The object-space position of a vertex after the shader's VertexOffset.
float3 SurfaceObjectPosition(MeshVertexInput input, uint instanceIndex, uint materialSlot)
{
	float3 position = input.Position;
#ifdef SURFACE_HAS_VERTEX_OFFSET
	position += VertexOffset(MakeVertexInput(input, instanceIndex), LoadMaterialParams(materialSlot));
#endif
	return position;
}

// The material's SurfaceOutput at a pixel: the built-in values, then the user's Surface.
SurfaceOutput EvaluateSurface(MaterialRecord material, uint materialSlot, SurfaceInput input)
{
	SurfaceOutput output = BuiltinSurfaceOutput(material, input.Uv);
	Surface(input, LoadMaterialParams(materialSlot), output);
	return output;
}

#endif // SURFACE_CONTRACT_HOOKS
