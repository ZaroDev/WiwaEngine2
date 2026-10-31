#pragma once

#include <math/math.h>

namespace wiwa
{
	// Bits of GpuMaterialRecord::Flags
	constexpr u32 MATERIAL_ALPHA_MASK_BIT = 1u << 0;  // alpha test against AlphaCutoff
	constexpr u32 MATERIAL_ALPHA_BLEND_BIT = 1u << 1; // alpha blended
	constexpr u32 MATERIAL_DOUBLE_SIDED_BIT = 1u << 2;
	constexpr u32 MATERIAL_CUSTOM_BIT = 1u << 4;       // drawn with its surface shader (the custom record of the slot is valid)

	// Bits of GpuMaterialRecord::TextureMask: which textures are the real ones (not the fallbacks).
	constexpr u32 MATERIAL_HAS_BASE_COLOR = 1u << 0;
	constexpr u32 MATERIAL_HAS_NORMAL = 1u << 1;
	constexpr u32 MATERIAL_HAS_ORM = 1u << 2;
	constexpr u32 MATERIAL_HAS_EMISSIVE = 1u << 3;

	// One material in the structured buffer the shaders index by slot. 64 bytes; mirrored by MaterialRecord in Common.hlsli.
	struct GpuMaterialRecord
	{
		glm::vec4 BaseColor{ 1.0f };
		glm::vec3 Emissive{ 0.0f };
		float Metallic = 0.0f;
		float Roughness = 0.8f;
		float AlphaCutoff = 0.5f;
		u32 Flags = 0;
		u32 TextureMask = 0;
		glm::vec4 Extra{ 0.0f }; // clearcoat, clearcoat roughness, sheen intensity, transmission: stored, not shaded yet
	};
	static_assert(sizeof(GpuMaterialRecord) == 64, "GpuMaterialRecord is read as a structured buffer element of 64 bytes");

	// Per material slot, in a second structured buffer: where a custom material's parameter block is in the parameter byte
	// buffer. 16 bytes; mirrored by SurfaceRecord in surface_contract.hlsli. Zeros for a slot without a ready surface shader.
	struct GpuCustomRecord
	{
		u32 ParamOffset = 0; // bytes, a multiple of 16
		u32 ParamSize = 0;   // bytes, the shader's BlockSize
		u32 Reserved[2]{};
	};
	static_assert(sizeof(GpuCustomRecord) == 16, "GpuCustomRecord is read as a structured buffer element of 16 bytes");
	constexpr u32 PARAM_BLOCK_ALIGNMENT = 16;

	// Where the material textures sit in the shaders: descriptor set 1 on Vulkan (the frame data is set 0), and registers
	// t10..t13 / s14 on D3D (above every register of set 0: NVRHI refuses layouts whose register intervals overlap) (shader model 5.0 has no register spaces, so the registers are unique across layouts).
	constexpr u32 MATERIAL_BINDING_BASE_COLOR = 10;
	constexpr u32 MATERIAL_BINDING_NORMAL = 11;
	constexpr u32 MATERIAL_BINDING_ORM = 12;
	constexpr u32 MATERIAL_BINDING_EMISSIVE = 13;
	constexpr u32 MATERIAL_BINDING_SAMPLER = 14;

	// The custom-material layout (descriptor set 3 on Vulkan, see surface_contract.hlsli): t16..t19 the @param textures, t20 the
	// custom records, t21 the parameter bytes, s15 the user sampler (shader model 5.0 has only s0..s15).
	constexpr u32 CUSTOM_BINDING_FIRST_TEXTURE = 16;
	constexpr u32 CUSTOM_BINDING_RECORDS = 20;
	constexpr u32 CUSTOM_BINDING_PARAMS = 21;
	constexpr u32 CUSTOM_BINDING_SAMPLER = 15;
	constexpr u32 CUSTOM_DESCRIPTOR_SET = 3; // the custom layout's index among a pipeline's binding layouts
}
