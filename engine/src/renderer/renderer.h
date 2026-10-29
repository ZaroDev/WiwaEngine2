#pragma once

#include <deque>
#include <vector>

#include <nvrhi/nvrhi.h>

#include <core/common.h>

#include "backend/render_backend.h"
#include "pass_profiler.h"
#include "render_pass.h"
#include "render_queue.h"
#include "resources/gpu_resource_cache.h"
#include "scene/render_scene.h"
#include "scene/scene_render_data.h"
#include "texture_registry.h"
#include "upload_queue.h"
#include "viewport_target.h"

namespace wiwa
{
	class ShaderLibrary;

	// The render thread's renderer: owns the backend and the passes, and turns a batch into a presented frame.
	// Everything here runs on the render thread.
	class Renderer
	{
	public:
		Renderer();
		~Renderer();

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		bool initialize(const RendererDescription& description);
		void shutdown();

		bool add_pass(Scope<IRenderPass> pass);

		// Draws one frame for the batch (the batch's commands were applied already). Returns false when the renderer has
		// failed and cannot go on (device lost, a pass failed, the swapchain cannot be rebuilt).
		bool render_frame(const RenderBatch& batch);

		nvrhi::IDevice* get_device() const { return m_Backend ? m_Backend->get_device() : nullptr; }
		ShaderLibrary* get_shader_library() const { return m_Shaders.get(); }
		UploadQueue& get_upload_queue() { return m_Uploads; }
		RenderScene& get_scene() { return m_Scene; }
		GpuResourceCache& get_cache() { return *m_Cache; }
		SceneRenderData& get_scene_data() { return m_SceneData; }
		AssetManager* get_assets() const { return m_Description.Assets; }
		TextureRegistry& get_textures() { return m_Textures; }
		ViewportTarget& get_viewport() { return m_Viewport; }
		nvrhi::IFramebuffer* get_reference_framebuffer() const { return m_Backend ? m_Backend->get_reference_framebuffer() : nullptr; }

		// The size the scene is rendered at (the Viewport panel's size). The target is made, or made again, at the start of the next
		// frame. 0 in either dimension changes nothing (a hidden panel keeps its target).
		void set_viewport_size(u32 width, u32 height);
		// True: the scene is drawn into the viewport target at the size requested above and the UI shows it (the editor). False, the
		// default: the viewport target follows the window and the blit pass draws it fullscreen (a game).
		// Applies the quality settings (render thread; commands run between frames). Vsync and the copy queue take effect at once.
		void set_quality(const RenderQuality& quality);
		const RenderQuality& get_quality() const { return m_Quality; }
		void set_scene_in_viewport(bool inViewport) { m_SceneInViewport = inViewport; }
		bool is_scene_in_viewport() const { return m_SceneInViewport; }
		GraphicsApi get_api() const { return m_Description.Api; }
		std::string get_adapter_name() const { return m_Backend ? m_Backend->get_adapter_name() : std::string{}; }
		u32 get_width() const { return m_Backend ? m_Backend->get_width() : 0; }
		u32 get_height() const { return m_Backend ? m_Backend->get_height() : 0; }
		bool has_copy_queue() const { return m_Backend && m_Backend->has_copy_queue(); }

		// Takes effect at the start of the next frame.
		void set_vsync(bool enabled) { m_RequestedVsync = enabled; }
		bool is_vsync_enabled() const { return m_RequestedVsync; }

		// True once every pass can render (until then only the background colour is shown).
		bool are_passes_ready() const { return m_PassesReady; }

		// Averaged per-pass timings, in pass order. Empty until the passes have rendered once.
		const std::vector<PassTiming>& get_pass_timings() const { return m_Profiler.get_timings(); }
		bool are_gpu_timings_available() const { return m_Profiler.are_gpu_timings_available(); }

	private:
		bool apply_vsync(u32 width, u32 height);
		void update_viewport();
		bool resize_backend(u32 width, u32 height);
		PassInitializationDescription make_pass_description();
		FrameDescription make_frame_description(nvrhi::IFramebuffer* backbuffer, const RenderBatch& batch);

		// Frames in flight: block until fewer than MaxFramesInFlight frames are still running on the GPU.
		void wait_for_frame_slot();
		// Marks the point the GPU has to reach for the frame just submitted to count as finished.
		void mark_frame_submitted();

	private:
		RendererDescription m_Description{};
		u64 m_FrameIndex = 0;
		float m_Time = 0.0f;
		bool m_RequestedVsync = true;
		u32 m_PresentResizeStreak = 0; // consecutive frames whose present asked for a resize at an unchanged size

		Scope<IRenderBackend> m_Backend{};
		Scope<ShaderLibrary> m_Shaders{};
		UploadQueue m_Uploads{};
		RenderScene m_Scene{};
		Scope<GpuResourceCache> m_Cache{};
		SceneRenderData m_SceneData{};
		TextureRegistry m_Textures{};
		ViewportTarget m_Viewport{};
		u32 m_RequestedViewportWidth = 0;
		u32 m_RequestedViewportHeight = 0;
		RenderQuality m_Quality{};
		bool m_SceneInViewport = false;
		bool m_ViewportFailed = false; // creating the target at the requested size failed: not tried again until the size changes
		nvrhi::CommandListHandle m_CommandList{};
		std::vector<Scope<IRenderPass>> m_Passes{};
		PassProfiler m_Profiler{};

		// One event query per submitted frame that has not been waited on yet, oldest first.
		std::deque<nvrhi::EventQueryHandle> m_FramesInFlight{};
		std::vector<nvrhi::EventQueryHandle> m_QueryPool{}; // queries ready for reuse

		bool m_PassesReady = false;
		bool m_Initialized = false;
	};
}
