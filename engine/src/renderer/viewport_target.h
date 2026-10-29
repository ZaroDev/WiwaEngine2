#pragma once

#include <nvrhi/nvrhi.h>

#include <math/math.h>

namespace wiwa
{
	// What the scene is drawn into: an HDR color target, a depth target and the 8-bit LDR result that the editor shows in its Viewport
	// panel (and a game draws fullscreen). Render thread only.
	class ViewportTarget
	{
	public:
		static constexpr u32 MAX_SIZE = 16384;

		// The size a request is turned into: at least 1, at most MAX_SIZE.
		static u32 clamp_dimension(u32 size);

		// Creates the targets at the (clamped) size; a previous set is dropped (the frames still using it keep it alive).
		bool create(nvrhi::IDevice* device, u32 width, u32 height);
		void destroy();

		bool valid() const { return m_Hdr != nullptr; }
		u32 width() const { return m_Width; }
		u32 height() const { return m_Height; }

		nvrhi::ITexture* hdr() const { return m_Hdr; }     // RGBA16F
		nvrhi::ITexture* depth() const { return m_Depth; } // D32
		nvrhi::ITexture* ldr() const { return m_Ldr; }     // RGBA8, sRGB encoded by the resolve shader
		// Automatic exposure: one float (the current exposure multiplier, 0 until the exposure pass has written it) and the 256-bin
		// luminance histogram it is measured from.
		nvrhi::IBuffer* exposure_buffer() const { return m_Exposure; }
		nvrhi::IBuffer* histogram_buffer() const { return m_Histogram; }
		nvrhi::IFramebuffer* hdr_framebuffer() const { return m_HdrFramebuffer; }     // HDR + depth
		nvrhi::IFramebuffer* depth_framebuffer() const { return m_DepthFramebuffer; } // depth only
		nvrhi::IFramebuffer* ldr_framebuffer() const { return m_LdrFramebuffer; }

		// Where the pixels are: counts up on every create, so a pass that cached a framebuffer notices.
		u32 generation() const { return m_Generation; }

	private:
		nvrhi::TextureHandle m_Hdr{};
		nvrhi::TextureHandle m_Depth{};
		nvrhi::TextureHandle m_Ldr{};
		nvrhi::BufferHandle m_Exposure{};
		nvrhi::BufferHandle m_Histogram{};
		nvrhi::FramebufferHandle m_HdrFramebuffer{};
		nvrhi::FramebufferHandle m_DepthFramebuffer{};
		nvrhi::FramebufferHandle m_LdrFramebuffer{};
		u32 m_Width = 0;
		u32 m_Height = 0;
		u32 m_Generation = 0;
	};
}
