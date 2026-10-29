#ifndef COMMON_HLSLI
#define COMMON_HLSLI

// Shared by every shader. The same HLSL is compiled two ways:
//   D3D11:  D3DCompile at load time (shader model 5.0, DXBC)
//   Vulkan: DXC with -spirv at build time, with VULKAN defined
//
// Register numbers are the NVRHI binding slots. DXC maps bN / tN / sN to the same
// descriptor binding number (set 0), and the C++ side uses zero binding offsets, so slots are
// unique across register types within one pass: e.g. CB 0 + SRVs 1..3, never CB 0 + SRV 0.
//
// Matrices arrive column-major from GLM, which is HLSL's default packing, so mul(M, v)
// matches on both backends (DXC adjusts the SPIR-V layout to behave like D3D).

// Usage:  struct MyData { ... };   PUSH_CONSTANTS(MyData, Constants, b1);
// The data is then read as Constants.Field on both backends.
#ifdef VULKAN
	// Real push constants. DXC only accepts [[vk::push_constant]] on a global variable of
	// struct type (not on a cbuffer); the register is meaningless, the data lives in the command buffer.
	#define PUSH_CONSTANTS(type, name, slot) [[vk::push_constant]] type name
#else
	// NVRHI emulates push constants with a constant buffer bound at the slot used in C++.
	#define PUSH_CONSTANTS(type, name, slot) cbuffer name##Buffer : register(slot) { type name; }
#endif

// Puts a resource in a given descriptor set on Vulkan (layouts after the first are further sets); D3D has no equivalent and
// relies on unique register numbers.
#ifdef VULKAN
	#define VK_BINDING(slot, setIndex) [[vk::binding(slot, setIndex)]]
#else
	#define VK_BINDING(slot, setIndex)
#endif

// Storage image format. Vulkan needs it spelled out (otherwise the device needs the
// shaderStorageImageWriteWithoutFormat feature); D3D11 takes it from the view.
#ifdef VULKAN
	#define IMAGE_FORMAT(format) [[vk::image_format(format)]]
#else
	#define IMAGE_FORMAT(format)
#endif

// Octahedral normal encoding: a unit vector <-> two components in [-1, 1].
// Halves the size of the normal G-buffer target compared to storing xyz.
float2 OctahedralEncode(float3 n)
{
	n /= (abs(n.x) + abs(n.y) + abs(n.z));
	float2 e = n.xy;
	if (n.z < 0.0)
	{
		e = (1.0 - abs(e.yx)) * float2(e.x >= 0.0 ? 1.0 : -1.0, e.y >= 0.0 ? 1.0 : -1.0);
	}
	return e;
}

float3 OctahedralDecode(float2 e)
{
	float3 n = float3(e.x, e.y, 1.0 - abs(e.x) - abs(e.y));
	if (n.z < 0.0)
	{
		n.xy = (1.0 - abs(n.yx)) * float2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
	}
	return normalize(n);
}

// One entry per object in the scene, read with SV_InstanceID: a whole mesh is drawn with a single call.
// Mirrors SceneGeometry::InstanceData. The model matrix is stored as four column vectors so nothing
// depends on matrix packing rules.
struct InstanceData
{
	float4 ModelColumn0;
	float4 ModelColumn1;
	float4 ModelColumn2;
	float4 ModelColumn3;
	float4 Albedo;
	float4 Material; // x = metallic, y = roughness, z = ambient occlusion, w = translucency
	float4 Layers;   // x = clearcoat, y = clearcoat roughness, z = sheen intensity, w = sheen roughness
	float4 Emissive; // rgb = emitted light, w = transmission
};

#endif // COMMON_HLSLI
