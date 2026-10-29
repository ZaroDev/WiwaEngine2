#include <pch.h>
#include "viewport_target.h"

#include <algorithm>

namespace wiwa
{
	u32 ViewportTarget::clamp_dimension(u32 size)
	{
		return std::clamp(size, 1u, MAX_SIZE);
	}

	bool ViewportTarget::create(nvrhi::IDevice* device, u32 width, u32 height)
	{
		width = clamp_dimension(width);
		height = clamp_dimension(height);
		destroy();

		nvrhi::TextureDesc hdr;
		hdr.width = width;
		hdr.height = height;
		hdr.format = nvrhi::Format::RGBA16_FLOAT;
		hdr.isRenderTarget = true;
		hdr.isShaderResource = true;
		hdr.initialState = nvrhi::ResourceStates::ShaderResource;
		hdr.keepInitialState = true;
		hdr.useClearValue = true;
		hdr.clearValue = nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f);
		hdr.debugName = "Viewport HDR";

		nvrhi::TextureDesc depth;
		depth.width = width;
		depth.height = height;
		depth.format = nvrhi::Format::D32;
		depth.isRenderTarget = true;
		depth.isShaderResource = true;
		depth.isTypeless = true; // D3D11 cannot sample a D32_FLOAT texture: NVRHI makes it R32_TYPELESS with depth and shader views
		depth.initialState = nvrhi::ResourceStates::DepthWrite;
		depth.keepInitialState = true;
		depth.useClearValue = true;
		depth.clearValue = nvrhi::Color(1.0f, 0.0f, 0.0f, 0.0f);
		depth.debugName = "Viewport depth";

		nvrhi::TextureDesc ldr;
		ldr.width = width;
		ldr.height = height;
		ldr.format = nvrhi::Format::RGBA8_UNORM;
		ldr.isRenderTarget = true;
		ldr.isShaderResource = true;
		ldr.initialState = nvrhi::ResourceStates::ShaderResource;
		ldr.keepInitialState = true;
		ldr.useClearValue = true;
		ldr.clearValue = nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f);
		ldr.debugName = "Viewport LDR";

		auto make_buffer = [&](u32 elements, const char* name)
		{
			nvrhi::BufferDesc description;
			description.byteSize = elements * sizeof(u32);
			description.structStride = sizeof(u32);
			description.canHaveUAVs = true;
			description.initialState = nvrhi::ResourceStates::ShaderResource;
			description.keepInitialState = true;
			description.debugName = name;
			return device->createBuffer(description);
		};
		m_Exposure = make_buffer(1, "Viewport exposure");
		m_Histogram = make_buffer(256, "Viewport luminance histogram");

		m_Hdr = device->createTexture(hdr);
		m_Depth = device->createTexture(depth);
		m_Ldr = device->createTexture(ldr);
		if (!m_Hdr || !m_Depth || !m_Ldr || !m_Exposure || !m_Histogram)
		{
			WI_LOG(Error, "Renderer", "could not create the {0}x{1} viewport targets", width, height);
			destroy();
			return false;
		}

		nvrhi::FramebufferDesc hdrFramebuffer;
		hdrFramebuffer.addColorAttachment(m_Hdr);
		hdrFramebuffer.setDepthAttachment(m_Depth);
		m_HdrFramebuffer = device->createFramebuffer(hdrFramebuffer);

		nvrhi::FramebufferDesc depthFramebuffer;
		depthFramebuffer.setDepthAttachment(m_Depth);
		m_DepthFramebuffer = device->createFramebuffer(depthFramebuffer);

		nvrhi::FramebufferDesc ldrFramebuffer;
		ldrFramebuffer.addColorAttachment(m_Ldr);
		m_LdrFramebuffer = device->createFramebuffer(ldrFramebuffer);

		if (!m_HdrFramebuffer || !m_DepthFramebuffer || !m_LdrFramebuffer)
		{
			WI_LOG(Error, "Renderer", "could not create the viewport framebuffers");
			destroy();
			return false;
		}

		m_Width = width;
		m_Height = height;
		m_Generation++;
		return true;
	}

	void ViewportTarget::destroy()
	{
		m_HdrFramebuffer = nullptr;
		m_DepthFramebuffer = nullptr;
		m_LdrFramebuffer = nullptr;
		m_Exposure = nullptr;
		m_Histogram = nullptr;
		m_Hdr = nullptr;
		m_Depth = nullptr;
		m_Ldr = nullptr;
		m_Width = 0;
		m_Height = 0;
	}
}
