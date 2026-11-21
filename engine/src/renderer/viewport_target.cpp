#include <pch.h>
#include "viewport_target.h"

#include <algorithm>
#include <atomic>

namespace wiwa
{
	u32 ViewportTarget::clamp_dimension(u32 size)
	{
		return std::clamp(size, 1u, MAX_SIZE);
	}

	u32 ViewportTarget::next_uid()
	{
		static std::atomic<u32> counter{ 1 };
		return counter.fetch_add(1);
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
		depth.format = nvrhi::Format::D32S8;
		depth.isRenderTarget = true;
		depth.isShaderResource = true;
		depth.isTypeless = true; // D3D11 cannot sample a D32_FLOAT_S8X24 texture: NVRHI makes it R32G8X24_TYPELESS with depth, depth-read and stencil-read views
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

		nvrhi::TextureDesc ao;
		ao.width = width;
		ao.height = height;
		ao.format = nvrhi::Format::R8_UNORM;
		ao.isUAV = true; // written by compute shaders
		ao.initialState = nvrhi::ResourceStates::ShaderResource;
		ao.keepInitialState = true;
		ao.debugName = "AO raw";
		m_AoRaw = device->createTexture(ao);
		ao.debugName = "AO blurred";
		m_Ao = device->createTexture(ao);

		// The distances are halvings of the size down to 1 pixel, at most MAX_AO_DEPTH_LEVELS.
		m_AoDepthLevels = 1;
		while (m_AoDepthLevels < MAX_AO_DEPTH_LEVELS && (std::max(width, height) >> m_AoDepthLevels) > 0)
		{
			m_AoDepthLevels++;
		}
		ao.format = nvrhi::Format::R32_FLOAT;
		ao.mipLevels = m_AoDepthLevels;
		ao.debugName = "AO distances";
		m_AoDistance = device->createTexture(ao);

		bool bloomOk = true;
		for (u32 level = 0; level < MAX_BLOOM_MIPS; level++)
		{
			nvrhi::TextureDesc description;
			description.width = std::max(1u, width >> (level + 1));
			description.height = std::max(1u, height >> (level + 1));
			description.format = nvrhi::Format::RGBA16_FLOAT;
			description.isUAV = true; // written by compute shaders
			description.initialState = nvrhi::ResourceStates::ShaderResource;
			description.keepInitialState = true;
			description.debugName = "Bloom down " + std::to_string(level);
			m_BloomDown[level] = device->createTexture(description);
			bloomOk = bloomOk && m_BloomDown[level] != nullptr;
			if (level + 1 < MAX_BLOOM_MIPS)
			{
				description.debugName = "Bloom up " + std::to_string(level);
				m_BloomUp[level] = device->createTexture(description);
				bloomOk = bloomOk && m_BloomUp[level] != nullptr;
			}
		}

		nvrhi::TextureDesc normal;
		normal.width = width;
		normal.height = height;
		normal.format = nvrhi::Format::RG16_FLOAT;
		normal.isRenderTarget = true;
		normal.isShaderResource = true;
		normal.initialState = nvrhi::ResourceStates::ShaderResource;
		normal.keepInitialState = true;
		normal.useClearValue = true;
		normal.clearValue = nvrhi::Color(0.0f, 0.0f, 0.0f, 0.0f); // the encoding of +Z
		normal.debugName = "Viewport normals";
		m_Normal = device->createTexture(normal);

		nvrhi::TextureDesc velocity = normal; // same format and usage as the normals
		velocity.clearValue = nvrhi::Color(0.0f, 0.0f, 0.0f, 0.0f); // no motion
		velocity.debugName = "Viewport velocity";
		m_Velocity = device->createTexture(velocity);

		m_Hdr = device->createTexture(hdr);
		m_Depth = device->createTexture(depth);
		m_Ldr = device->createTexture(ldr);
		if (!m_Hdr || !m_Depth || !m_Ldr || !m_Normal || !m_Velocity || !m_Exposure || !m_Histogram || !bloomOk || !m_AoRaw || !m_Ao || !m_AoDistance)
		{
			WI_LOG(Error, "Renderer", "could not create the {0}x{1} viewport targets", width, height);
			destroy();
			return false;
		}

		nvrhi::FramebufferDesc hdrFramebuffer;
		hdrFramebuffer.addColorAttachment(m_Hdr);
		hdrFramebuffer.setDepthAttachment(m_Depth);
		m_HdrFramebuffer = device->createFramebuffer(hdrFramebuffer);

		nvrhi::FramebufferDesc hdrColorFramebuffer;
		hdrColorFramebuffer.addColorAttachment(m_Hdr);
		m_HdrColorFramebuffer = device->createFramebuffer(hdrColorFramebuffer);

		nvrhi::FramebufferDesc depthFramebuffer;
		depthFramebuffer.setDepthAttachment(m_Depth);
		m_DepthFramebuffer = device->createFramebuffer(depthFramebuffer);

		nvrhi::FramebufferDesc ldrFramebuffer;
		ldrFramebuffer.addColorAttachment(m_Ldr);
		m_LdrFramebuffer = device->createFramebuffer(ldrFramebuffer);

		nvrhi::FramebufferDesc ldrDepthFramebuffer;
		ldrDepthFramebuffer.addColorAttachment(m_Ldr);
		ldrDepthFramebuffer.setDepthAttachment(m_Depth);
		m_LdrDepthFramebuffer = device->createFramebuffer(ldrDepthFramebuffer);

		nvrhi::FramebufferDesc prepassFramebuffer;
		prepassFramebuffer.addColorAttachment(m_Normal);
		prepassFramebuffer.addColorAttachment(m_Velocity);
		prepassFramebuffer.setDepthAttachment(m_Depth);
		m_PrepassFramebuffer = device->createFramebuffer(prepassFramebuffer);

		nvrhi::FramebufferDesc velocityFramebuffer;
		velocityFramebuffer.addColorAttachment(m_Velocity);
		velocityFramebuffer.setDepthAttachment(m_Depth);
		m_VelocityFramebuffer = device->createFramebuffer(velocityFramebuffer);

		if (!m_HdrFramebuffer || !m_HdrColorFramebuffer || !m_DepthFramebuffer || !m_LdrFramebuffer || !m_LdrDepthFramebuffer || !m_PrepassFramebuffer || !m_VelocityFramebuffer)
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

	bool ViewportTarget::ensure_decal_targets(nvrhi::IDevice* device)
	{
		if (has_decal_targets())
		{
			return true;
		}
		if (!valid())
		{
			return false;
		}

		nvrhi::TextureDesc description;
		description.width = m_Width;
		description.height = m_Height;
		description.isRenderTarget = true;
		description.isShaderResource = true;
		description.initialState = nvrhi::ResourceStates::ShaderResource;
		description.keepInitialState = true;
		description.useClearValue = true;
		description.clearValue = nvrhi::Color(0.0f, 0.0f, 0.0f, 0.0f);

		description.format = nvrhi::Format::SRGBA8_UNORM;
		description.debugName = "Decal albedo";
		m_DecalAlbedo = device->createTexture(description);
		description.format = nvrhi::Format::RGBA8_UNORM;
		description.debugName = "Decal normal";
		m_DecalNormal = device->createTexture(description);
		description.debugName = "Decal ORM";
		m_DecalOrm = device->createTexture(description);
		description.format = nvrhi::Format::R11G11B10_FLOAT;
		description.debugName = "Decal emissive";
		m_DecalEmissive = device->createTexture(description);

		nvrhi::FramebufferDesc framebuffer;
		if (m_DecalAlbedo && m_DecalNormal && m_DecalOrm && m_DecalEmissive)
		{
			framebuffer.addColorAttachment(m_DecalAlbedo);
			framebuffer.addColorAttachment(m_DecalNormal);
			framebuffer.addColorAttachment(m_DecalOrm);
			framebuffer.addColorAttachment(m_DecalEmissive);
			m_DecalFramebuffer = device->createFramebuffer(framebuffer);
		}
		if (!m_DecalFramebuffer)
		{
			WI_LOG(Error, "Renderer", "could not create the {0}x{1} decal targets", m_Width, m_Height);
			release_decal_targets();
			return false;
		}
		m_DecalGeneration++;
		return true;
	}

	void ViewportTarget::release_decal_targets()
	{
		const bool had = m_DecalAlbedo != nullptr || m_DecalFramebuffer != nullptr;
		m_DecalFramebuffer = nullptr;
		m_DecalAlbedo = nullptr;
		m_DecalNormal = nullptr;
		m_DecalOrm = nullptr;
		m_DecalEmissive = nullptr;
		if (had)
		{
			m_DecalGeneration++;
		}
	}

	bool ViewportTarget::ensure_taa_targets(nvrhi::IDevice* device)
	{
		m_TaaUse = m_Tick;
		if (m_TaaHistory[0] != nullptr)
		{
			return true;
		}
		if (!valid())
		{
			return false;
		}

		nvrhi::TextureDesc history;
		history.width = m_Width;
		history.height = m_Height;
		history.isUAV = true;
		history.isShaderResource = true;
		history.initialState = nvrhi::ResourceStates::ShaderResource;
		history.keepInitialState = true;
		bool ok = true;
		for (u32 i = 0; i < 2; i++)
		{
			history.format = nvrhi::Format::RGBA16_FLOAT;
			history.debugName = "TAA history " + std::to_string(i);
			m_TaaHistory[i] = device->createTexture(history);
			history.format = nvrhi::Format::R32_FLOAT;
			history.debugName = "TAA depth history " + std::to_string(i);
			m_TaaDepthHistory[i] = device->createTexture(history);
			ok = ok && m_TaaHistory[i] != nullptr && m_TaaDepthHistory[i] != nullptr;
		}
		if (!ok)
		{
			WI_LOG(Error, "Renderer", "could not create the {0}x{1} TAA history", m_Width, m_Height);
			release_taa_targets();
			return false;
		}
		m_TaaGeneration++;
		return true;
	}

	void ViewportTarget::release_taa_targets()
	{
		const bool had = m_TaaHistory[0] != nullptr;
		for (u32 i = 0; i < 2; i++)
		{
			m_TaaHistory[i] = nullptr;
			m_TaaDepthHistory[i] = nullptr;
		}
		if (had)
		{
			m_TaaGeneration++;
		}
	}

	bool ViewportTarget::ensure_post_scratch(nvrhi::IDevice* device)
	{
		m_ScratchUse = m_Tick;
		if (m_PostScratch != nullptr)
		{
			return true;
		}
		if (!valid())
		{
			return false;
		}

		nvrhi::TextureDesc scratch;
		scratch.width = m_Width;
		scratch.height = m_Height;
		scratch.format = nvrhi::Format::RGBA16_FLOAT;
		scratch.isUAV = true;
		scratch.isShaderResource = true;
		scratch.initialState = nvrhi::ResourceStates::ShaderResource;
		scratch.keepInitialState = true;
		scratch.debugName = "Viewport post scratch";
		m_PostScratch = device->createTexture(scratch);
		if (m_PostScratch == nullptr)
		{
			WI_LOG(Error, "Renderer", "could not create the {0}x{1} post scratch image", m_Width, m_Height);
			return false;
		}
		m_ScratchGeneration++;
		return true;
	}

	void ViewportTarget::release_post_scratch()
	{
		if (m_PostScratch != nullptr)
		{
			m_PostScratch = nullptr;
			m_ScratchGeneration++;
		}
	}

	bool ViewportTarget::ensure_fxaa_target(nvrhi::IDevice* device)
	{
		m_FxaaUse = m_Tick;
		if (m_PreFxaa != nullptr)
		{
			return true;
		}
		if (!valid())
		{
			return false;
		}

		nvrhi::TextureDesc description = m_Ldr->getDesc(); // the same as the final image
		description.debugName = "Viewport tonemapped (before FXAA)";
		m_PreFxaa = device->createTexture(description);
		if (m_PreFxaa != nullptr)
		{
			nvrhi::FramebufferDesc framebuffer;
			framebuffer.addColorAttachment(m_PreFxaa);
			m_PreFxaaFramebuffer = device->createFramebuffer(framebuffer);
		}
		if (m_PreFxaa == nullptr || m_PreFxaaFramebuffer == nullptr)
		{
			WI_LOG(Error, "Renderer", "could not create the {0}x{1} FXAA input image", m_Width, m_Height);
			release_fxaa_target();
			return false;
		}
		m_FxaaGeneration++;
		return true;
	}

	void ViewportTarget::release_fxaa_target()
	{
		const bool had = m_PreFxaa != nullptr;
		m_PreFxaaFramebuffer = nullptr;
		m_PreFxaa = nullptr;
		if (had)
		{
			m_FxaaGeneration++;
		}
	}

	void ViewportTarget::trim_effect_targets()
	{
		m_Tick++;
		if (m_TaaHistory[0] != nullptr && m_Tick - m_TaaUse > EFFECT_TARGET_LINGER_FRAMES)
		{
			release_taa_targets();
		}
		if (m_PostScratch != nullptr && m_Tick - m_ScratchUse > EFFECT_TARGET_LINGER_FRAMES)
		{
			release_post_scratch();
		}
		if (m_PreFxaa != nullptr && m_Tick - m_FxaaUse > EFFECT_TARGET_LINGER_FRAMES)
		{
			release_fxaa_target();
		}
	}

	void ViewportTarget::destroy()
	{
		release_decal_targets();
		m_HdrFramebuffer = nullptr;
		m_HdrColorFramebuffer = nullptr;
		m_DepthFramebuffer = nullptr;
		m_LdrFramebuffer = nullptr;
		m_LdrDepthFramebuffer = nullptr;
		m_PreFxaaFramebuffer = nullptr;
		m_PreFxaa = nullptr;
		m_PrepassFramebuffer = nullptr;
		m_VelocityFramebuffer = nullptr;
		m_Velocity = nullptr;
		release_post_scratch();
		release_taa_targets();
		release_fxaa_target();
		m_Normal = nullptr;
		for (nvrhi::TextureHandle& texture : m_BloomDown)
		{
			texture = nullptr;
		}
		for (nvrhi::TextureHandle& texture : m_BloomUp)
		{
			texture = nullptr;
		}
		m_AoDistance = nullptr;
		m_AoRaw = nullptr;
		m_Ao = nullptr;
		m_Exposure = nullptr;
		m_Histogram = nullptr;
		m_Hdr = nullptr;
		m_Depth = nullptr;
		m_Ldr = nullptr;
		m_PreFxaa = nullptr;
		m_Width = 0;
		m_Height = 0;
	}
}
