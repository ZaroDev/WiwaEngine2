#pragma once
#include <core/common.h>
#include <rhi/rhi_common.h>

#include <string>

namespace wiwa
{
	enum class ResourceState : uint32_t 
	{
		UNDEFINED = 0, COMMON = BIT(1), VERTEX_BUFFER = BIT(2), INDEX_BUFFER = BIT(3), UNIFORM_BUFFER = BIT(4),
		SHADER_RESOURCE = BIT(5), STORAGE_READ_WRITE = BIT(6), INDIRECT_ARGUMENT = BIT(7),
		RENDER_TARGET = BIT(8), DEPTH_WRITE = BIT(9), DEPTH_READ = BIT(10),
		COPY_SOURCE = BIT(11), COPY_DEST = BIT(12), PRESENT = BIT(13)
	};
	ENUM_CLASS_FLAG_OPERATORS(ResourceState)

	enum class BufferUsage : uint32_t
	{
		NONE = 0, VERTEX = BIT(1), INDEX = BIT(2), UNIFORM = BIT(3), STORAGE = BIT(4), INDIRECT = BIT(5), COPY_SRC = BIT(6), COPY_DST = BIT(7)
	};
	ENUM_CLASS_FLAG_OPERATORS(BufferUsage)

	enum class MemoryDomain : uint8_t
	{
		UPLOAD,
		GPU_ONLY,
		READBACK
	};

	class IRHIResource
	{
	public:
		virtual ~IRHIResource() = default;
		virtual void set_debug_name(std::string name) = 0;
		virtual void* get_native_handle() = 0;
	};

	struct BufferDescription
	{
		uint64_t Size = 0;
		BufferUsage Usage = BufferUsage::NONE;
		MemoryDomain Domain = MemoryDomain::GPU_ONLY;
		uint32_t StructStride = 0;
		std::string DebugName = "Unknown buffer";
	};

	class IBuffer : public IRHIResource
	{
	public:
		virtual const BufferDescription& get_description() const = 0;
		virtual void* map() = 0;
		virtual void unmap() = 0;
	};

	
	enum class TextureDimension : uint8_t 
	{ 
		TEX_1D, TEX_1D_ARRAY, TEX2D, TEX_2D_ARRAY, TEX_CUBE, TEX_CUBE_ARRAY, TEX_3D 
	};

	enum class TextureUsage : uint32_t 
	{
		NONE = 0, SAMPLED = BIT(1), STORAGE = BIT(2), RENDER_TARGET = BIT(3), DEPTH_STENCIL = BIT(4), COPY_SRC = BIT(5), COPY_DST = BIT(6)
	};
	ENUM_CLASS_FLAG_OPERATORS(TextureUsage)

	struct TextureDescription 
	{
		TextureDimension Dimension = TextureDimension::TEX2D;
		Format       Format = Format::UNKNOWN;
		Extent3D     Extent{};
		uint32_t     MipLevels = 1;
		uint32_t     ArrayLayers = 1;          // multiplied by 6 internally for cube
		uint32_t     SampleCount = 1;
		TextureUsage Usage = TextureUsage::SAMPLED;
		ResourceState InitialState = ResourceState::COMMON;
		const Color* OptimizedClear = nullptr; // hint for D3D12/Metal
		std::string DebugName = "Unkown texture";
	};

	struct TextureViewDesc 
	{
		TextureDimension Dimension = TextureDimension::TEX2D;
		Format   Format = Format::UNKNOWN;     // Unknown = same as texture
		uint32_t BaseMip = 0, MipCount = 0;
		uint32_t BaseLayer = 0, LayerCount = 0;
		bool     DepthOnly = false;            // view only the depth plane of a D/S format
	};

	class ITextureView;

	class ITexture : public IRHIResource 
	{
	public:
		virtual const TextureDescription& get_description() const = 0;
		// Default view covering the whole texture (cached by the backend).
		virtual ITextureView* get_default_get_descriptionview() = 0;
		virtual Ref<ITextureView> create_view(const TextureViewDesc& desc) = 0;
	};

	class ITextureView : public IRHIResource 
	{
	public:
		virtual const TextureViewDesc& get_description() const = 0;
		virtual ITexture* GetTexture() const = 0;
	};

	// ---- Sampler -----------------------------------------------------------------------------
	enum class FilterMode : uint8_t 
	{ 
		NEAREST, LINEAR 
	};
	enum class WrapMode : uint8_t 
	{ 
		REPEAT, MIRROR_REPEAT, CLAMP_TO_EDGE, CLAMP_TO_BORDER, MIRROR_CLAMP_TO_EDGE
	};
	enum class BorderColor : uint8_t 
	{ 
		TRANSPARENT_BLACK, OPAQUE_BLACK, OPAQUE_WHITE 
	};

	struct SamplerDescription
	{
		FilterMode MinFilter = FilterMode::LINEAR, MagFilter = FilterMode::LINEAR;
		FilterMode MipFilter = FilterMode::LINEAR;
		WrapMode   WrapU = WrapMode::REPEAT, WrapV = WrapMode::REPEAT, WrapW = WrapMode::REPEAT;
		float      MipLodBias = 0.f, MinLod = 0.f, MaxLod = 1000.f;
		float      MaxAnisotropy = 1.f;
		bool       CompareEnable = false;
		CompareFunc CompareFunc = CompareFunc::LESS;
		BorderColor BorderColor = BorderColor::TRANSPARENT_BLACK;
		std::string DebugName = "Unknown sampler";
	};

	class ISampler : public IRHIResource 
	{
	public:
		virtual const SamplerDescription& get_description() const = 0;
	};
	
}