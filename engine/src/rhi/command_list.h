#pragma once

#include "rhi_common.h"
#include "rhi_resource.h"
#include "pipeline.h"
#include "binding.h"

namespace wiwa
{
	enum class LoadOp : uint8_t
	{
		LOAD, CLEAR, DONT_CARE
	};

	enum class StoreOp : uint8_t
	{
		STORE, DONT_CARE
	};

	struct ColorAttachment
	{
		ITextureView* View = nullptr;
		ITextureView* ResolveTarget = nullptr;
		LoadOp LoadOp = LoadOp::CLEAR;
		StoreOp StoreOp = StoreOp::STORE;
		Color ClearColor{};
	};

	struct DepthStencilAttachment
	{
		ITextureView* View = nullptr;
		LoadOp DepthLoadOp = LoadOp::CLEAR, StencilLoadOp = LoadOp::DONT_CARE;
		StoreOp DepthStoreOp = StoreOp::STORE, StencilStoreOp = StoreOp::DONT_CARE;
		float ClearDepth = 1.0f;
		uint8_t ClearStencil = 0;
		bool ReadOnly = false;
	};

	struct RenderPassDescription
	{
		ColorAttachment Colors[kMaxColorAttachments];
		uint32_t ColorCount = 0;
		DepthStencilAttachment DepthStencil;
		std::string DebugName = "Unknown render pass";
	};

	struct TextureBarrier 
	{
		ITexture* Texture = nullptr;
		ResourceState Before = ResourceState::UNDEFINED, After = ResourceState::COMMON;
		uint32_t BaseMip = 0, MipCount = kRemainingSize, BaseLayer = 0, LayerCount = kRemainingSize;
	};

	struct BufferBarrier 
	{
		IBuffer* Buffer = nullptr;
		ResourceState Before = ResourceState::UNDEFINED, After = ResourceState::COMMON;
	};

	struct BufferCopyRegion
	{
		uint64_t SrcOffset = 0, DstOffset = 0, Size = 0;
	};

	struct TextureSubresource
	{
		uint32_t Mip = 0, Layer = 0;
	};

	struct BufferTextureCopyRegion
	{
		uint64_t BufferOffset = 0;
		uint64_t BufferRowPitch = 0;
		uint32_t BufferImageHeight = 0;
		TextureSubresource Subresource;
		Offset3D TextureOffset;
		Extent3D Extent;
	};

	struct TextureCopyRegion 
	{
		TextureSubresource SrcSubresource, DstSubresource;
		Offset3D SrcOffset, DstOffset;
		Extent3D Extent;
	};

	class ICommandList : public IRHIResource
	{

	};
}