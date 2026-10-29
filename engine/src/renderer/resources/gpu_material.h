#pragma once

#include <math/math.h>

namespace wiwa
{
	// Bits of GpuMaterialRecord::Flags
	constexpr u32 MATERIAL_ALPHA_MASK_BIT = 1u << 0;  // alpha test against AlphaCutoff
	constexpr u32 MATERIAL_ALPHA_BLEND_BIT = 1u << 1; // alpha blended
	constexpr u32 MATERIAL_DOUBLE_SIDED_BIT = 1u << 2;

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

	// Where the material textures sit in the shaders: descriptor set 1 on Vulkan (the frame data is set 0), and registers
	// t8..t11 / s12 on D3D (shader model 5.0 has no register spaces, so the registers are unique across layouts).
	constexpr u32 MATERIAL_BINDING_BASE_COLOR = 8;
	constexpr u32 MATERIAL_BINDING_NORMAL = 9;
	constexpr u32 MATERIAL_BINDING_ORM = 10;
	constexpr u32 MATERIAL_BINDING_EMISSIVE = 11;
	constexpr u32 MATERIAL_BINDING_SAMPLER = 12;
}
