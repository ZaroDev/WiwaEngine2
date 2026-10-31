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
		static constexpr u32 MAX_AO_DEPTH_LEVELS = 5;

		// The size a request is turned into: at least 1, at most MAX_SIZE.
		static u32 clamp_dimension(u32 size);

		// Creates the targets at the (clamped) size; a previous set is dropped (the frames still using it keep it alive).
		bool create(nvrhi::IDevice* device, u32 width, u32 height);
		void destroy();

		bool valid() const { return m_Hdr != nullptr; }
		u32 width() const { return m_Width; }
		u32 height() const { return m_Height; }

		nvrhi::ITexture* hdr() const { return m_Hdr; }     // RGBA16F
		nvrhi::ITexture* depth() const { return m_Depth; } // D32S8
		nvrhi::ITexture* ldr() const { return m_Ldr; }     // RGBA8, sRGB encoded by the resolve shader
		// Automatic exposure: one float (the current exposure multiplier, 0 until the exposure pass has written it) and the 256-bin
		// luminance histogram it is measured from.
		nvrhi::IBuffer* exposure_buffer() const { return m_Exposure; }
		nvrhi::IBuffer* histogram_buffer() const { return m_Histogram; }
		// Ambient occlusion: the distance along the view direction of every pixel with a mip chain (R32F, ao_depth_levels() levels),
		// the raw occlusion and the blurred one (R8, 1 = open, 0 = blocked) that the forward pass reads. Written by compute shaders.
		nvrhi::ITexture* ao_distance() const { return m_AoDistance; }
		nvrhi::ITexture* ao_raw() const { return m_AoRaw; }
		nvrhi::ITexture* ao() const { return m_Ao; }
		u32 ao_depth_levels() const { return m_AoDepthLevels; }
		// The bloom chain: down(i) is half the size of the level before it (the first is half the viewport), up(i) is the spread-back
		// level i. up(0) is the finished bloom. Level sizes are at least 1x1. RGBA16F, written by compute shaders.
		nvrhi::ITexture* bloom_down(u32 level) const { return m_BloomDown[level]; }
		nvrhi::ITexture* bloom_up(u32 level) const { return level + 1 < MAX_BLOOM_MIPS ? m_BloomUp[level].Get() : nullptr; }
		nvrhi::ITexture* bloom() const { return m_BloomUp[0]; }
		u32 bloom_width(u32 level) const { return std::max(1u, m_Width >> (level + 1)); }
		u32 bloom_height(u32 level) const { return std::max(1u, m_Height >> (level + 1)); }
		nvrhi::IFramebuffer* hdr_framebuffer() const { return m_HdrFramebuffer; }     // HDR + depth
		nvrhi::IFramebuffer* depth_framebuffer() const { return m_DepthFramebuffer; } // depth only
		// The world normal of the nearest surface, facing the viewer, octahedral encoded (RG16F), written by the depth prepass.
		nvrhi::ITexture* normal() const { return m_Normal; }
		// How far the surface of every pixel moved on the screen since the previous frame, as a uv delta (current minus previous, unjittered;
		// RG16F, zero where nothing was drawn), written by the depth prepass and the transparent velocity pass.
		nvrhi::ITexture* velocity() const { return m_Velocity; }
		// TAA: the resolved image of the last two frames it ran (RGBA16F) and the depth of those frames (R32F), written by the TAA compute
		// pass. Index i is for the pass to ping-pong between; zero until the pass has written it.
		nvrhi::ITexture* taa_history(u32 i) const { return m_TaaHistory[i & 1]; }
		nvrhi::ITexture* taa_depth_history(u32 i) const { return m_TaaDepthHistory[i & 1]; }
		nvrhi::IFramebuffer* prepass_framebuffer() const { return m_PrepassFramebuffer; } // normal + velocity + depth
		nvrhi::IFramebuffer* velocity_framebuffer() const { return m_VelocityFramebuffer; } // velocity + depth
		nvrhi::IFramebuffer* ldr_framebuffer() const { return m_LdrFramebuffer; }
		// ldr() and the depth/stencil target together: the selection outline's stencil mask and the debug draw pass use it.
		nvrhi::IFramebuffer* ldr_depth_framebuffer() const { return m_LdrDepthFramebuffer; }
		// The format to bind depth() with to sample its stencil plane (the depth plane is the default view).
		static constexpr nvrhi::Format STENCIL_VIEW_FORMAT = nvrhi::Format::X32G8_UINT;
		// Where the tonemap pass draws when FXAA runs: the image FXAA reads and writes to ldr(). Same format as ldr().
		nvrhi::ITexture* pre_fxaa() const { return m_PreFxaa; }
		nvrhi::IFramebuffer* pre_fxaa_framebuffer() const { return m_PreFxaaFramebuffer; }

		// Where the pixels are: counts up on every create, so a pass that cached a framebuffer notices.
		u32 generation() const { return m_Generation; }

	private:
		nvrhi::TextureHandle m_Hdr{};
		nvrhi::TextureHandle m_Depth{};
		nvrhi::TextureHandle m_Ldr{};
		nvrhi::TextureHandle m_AoDistance{};
		nvrhi::TextureHandle m_AoRaw{};
		nvrhi::TextureHandle m_Ao{};
		u32 m_AoDepthLevels = 1;
		nvrhi::TextureHandle m_BloomDown[MAX_BLOOM_MIPS]{};
		nvrhi::TextureHandle m_BloomUp[MAX_BLOOM_MIPS - 1]{};
		nvrhi::BufferHandle m_Exposure{};
		nvrhi::BufferHandle m_Histogram{};
		nvrhi::FramebufferHandle m_HdrFramebuffer{};
		nvrhi::FramebufferHandle m_DepthFramebuffer{};
		nvrhi::FramebufferHandle m_LdrFramebuffer{};
		nvrhi::FramebufferHandle m_LdrDepthFramebuffer{};
		nvrhi::TextureHandle m_Normal{};
		nvrhi::FramebufferHandle m_PrepassFramebuffer{};
		nvrhi::TextureHandle m_Velocity{};
		nvrhi::TextureHandle m_TaaHistory[2]{};
		nvrhi::TextureHandle m_TaaDepthHistory[2]{};
		nvrhi::FramebufferHandle m_VelocityFramebuffer{};
		nvrhi::TextureHandle m_PreFxaa{};
		nvrhi::FramebufferHandle m_PreFxaaFramebuffer{};
		u32 m_Width = 0;
		u32 m_Height = 0;
		u32 m_Generation = 0;
	};
}
