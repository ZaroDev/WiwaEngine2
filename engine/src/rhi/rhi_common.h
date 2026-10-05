#pragma once
#include <core/common.h>
#undef DOMAIN

namespace wiwa
{
	constexpr uint32_t kMaxColorAttachments = 8;
	constexpr uint32_t kMaxVertexBuffers = 16;
	constexpr uint32_t kMaxVertexAttributes = 16;
	constexpr uint32_t kMaxBindingSets = 4;
	constexpr uint32_t kRemainingSize = ~0u;

	enum class GraphicsAPI : uint8_t
	{
		NONE,
		OPENGL,
		VULKAN,
		D3D11,
		D3D12,
		METAL
	};

	enum class ShaderStage : uint8_t
	{
		NONE = BIT(0),
		VERTEX = BIT(1),
		PIXEL = BIT(2),
		COMPUTE = BIT(3),
		GEOMETRY = BIT(4),
		HULL = BIT(5),
		DOMAIN = BIT(6),

		ALL_GRAPHICS = VERTEX | PIXEL | GEOMETRY | HULL | DOMAIN,
		ALL = ALL_GRAPHICS | COMPUTE,
	};
	ENUM_CLASS_FLAG_OPERATORS(ShaderStage);

	enum class Format : uint8_t
	{
		UNKNOWN,
		R8_UNORM, RG8_UNORM, RGBA8_UNORM, RGBA8_SRGB, BGRA8_UNORM, BGRA8_SRGB,
		R16_FLOAT, RG16_FLOAT, RGBA16_FLOAT,
		R32_FLOAT, RG32_FLOAT, RGB32_FLOAT, RGBA32_FLOAT,
		R32_UINT, RG32_UINT, RGB32_UINT, RGBA32_UINT,
		R32_SINT, RG32_SINT, RGB32_SINT, RGBA32_SINT,
		R11G11B10_FLOAT, RGB10A2_UNORM,
		D16_UNORM, D32_FLOAT, D24_UNORM_S8_UINT, D32_FLOAT_S8_UINT,
		BC1_UNORM, BC1_SRGB, BC3_UNORM, BC3_SRGB, BC4_UNORM, BC5_UNORM, BC6H_UFLOAT, BC7_UNORM, BC7_SRGB,

		COUNT
	};

	enum class CompareFunc : uint8_t
	{
		NEVER, LESS, EQUAL, LESS_EQUAL, GREATER, NOT_EQUAL, GREATER_EQUAL, ALWAYS
	};

	struct Extent2D { uint32_t Width = 0, Height = 0; };
	struct Extent3D { uint32_t Width = 1, Height = 1, Depth = 1; };
	struct Offset3D { int32_t x = 0, y = 0, z = 0; };
	struct Rect { int32_t x = 0, y = 0; uint32_t Width = 0, Height = 0; };
	struct Viewport { float x = 0, y = 0, Width = 0, Height = 0, MinDepth = 0.f, MaxDepth = 1.f; };
	struct Color { float r = 0, g = 0, b = 0, a = 1; };
}