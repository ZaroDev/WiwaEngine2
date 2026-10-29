#pragma once

#include <algorithm>

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
		static constexpr u32 MAX_BLOOM_MIPS = 6;

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
		// The bloom chain: down(i) is half the size of the level before it (the first is half the viewport), up(i) is the spread-back
		// level i. up(0) is the finished bloom. Level sizes are at least 1x1. RGBA16F, written by compute shaders.
		nvrhi::ITexture* bloom_down(u32 level) const { return m_BloomDown[level]; }
		nvrhi::ITexture* bloom_up(u32 level) const { return level + 1 < MAX_BLOOM_MIPS ? m_BloomUp[level].Get() : nullptr; }
		nvrhi::ITexture* bloom() const { return m_BloomUp[0]; }
		u32 bloom_width(u32 level) const { return std::max(1u, m_Width >> (level + 1)); }
		u32 bloom_height(u32 level) const { return std::max(1u, m_Height >> (level + 1)); }
		nvrhi::IFramebuffer* hdr_framebuffer() const { return m_HdrFramebuffer; }     // HDR + depth
		nvrhi::IFramebuffer* depth_framebuffer() const { return m_DepthFramebuffer; } // depth only
		nvrhi::IFramebuffer* ldr_framebuffer() const { return m_LdrFramebuffer; }
		// Where the tonemap pass draws when FXAA runs: the image FXAA reads and writes to ldr(). Same format as ldr().
		nvrhi::ITexture* pre_fxaa() const { return m_PreFxaa; }
		nvrhi::IFramebuffer* pre_fxaa_framebuffer() const { return m_PreFxaaFramebuffer; }

		// Where the pixels are: counts up on every create, so a pass that cached a framebuffer notices.
		u32 generation() const { return m_Generation; }

	private:
		nvrhi::TextureHandle m_Hdr{};
		nvrhi::TextureHandle m_Depth{};
		nvrhi::TextureHandle m_Ldr{};
		nvrhi::TextureHandle m_BloomDown[MAX_BLOOM_MIPS]{};
		nvrhi::TextureHandle m_BloomUp[MAX_BLOOM_MIPS - 1]{};
		nvrhi::BufferHandle m_Exposure{};
		nvrhi::BufferHandle m_Histogram{};
		nvrhi::FramebufferHandle m_HdrFramebuffer{};
		nvrhi::FramebufferHandle m_DepthFramebuffer{};
		nvrhi::FramebufferHandle m_LdrFramebuffer{};
		nvrhi::TextureHandle m_PreFxaa{};
		nvrhi::FramebufferHandle m_PreFxaaFramebuffer{};
		u32 m_Width = 0;
		u32 m_Height = 0;
		u32 m_Generation = 0;
	};
}
