#pragma once

#include <deque>
#include <span>
#include <vector>

#include <nvrhi/nvrhi.h>

#include <core/common.h>

#include "backend/render_backend.h"
#include "pass_profiler.h"
#include "render_pass.h"
#include "render_pick.h"
#include "render_queue.h"
#include "environment/environment_cache.h"
#include "resources/gpu_resource_cache.h"
#include "scene/render_scene.h"
#include "scene/scene_render_data.h"
#include "texture_registry.h"
#include "upload_queue.h"
#include "view/readback.h"
#include "view/view_state.h"
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
		RenderScene& get_scene() { return m_MainView.Scene; }
		GpuResourceCache& get_cache() { return *m_Cache; }
		EnvironmentCache& get_environments() { return *m_Environments; }
		SceneRenderData& get_scene_data() { return m_MainView.Data; }
		AssetManager* get_assets() const { return m_Description.Assets; }
		TextureRegistry& get_textures() { return m_Textures; }

		// The pass of this type that was added, or null (for the render checks).
		template<typename T>
		T* get_pass() const
		{
			for (const Scope<IRenderPass>& pass : m_Passes)
			{
				if (T* found = dynamic_cast<T*>(pass.get()))
				{
					return found;
				}
			}
			return nullptr;
		}

		// Picking: the click to answer on the next frame (a newer one replaces it) and where the answer goes (null: nowhere).
		PickState& get_pick() { return m_Pick; }
		void set_pick_request(const PickRequest& request) { m_Pick.Pending = request; }
		void set_pick_channel(PickChannel* channel) { m_Pick.Channel = channel; }
		ViewportTarget& get_viewport() { return m_MainView.Target; }

		// Views: the main one (the Viewport panel, or the window) is always there; the others are made for RenderViews and drawn into targets of
		// their own with the same passes. add_view returns INVALID_VIEW at MAX_VIEWS live views or when the view cannot be set up.
		ViewState& get_main_view() { return m_MainView; }
		ViewState* find_view(ViewId id);
		ViewId add_view(ViewId id, const ViewDesc& desc);
		void remove_view(ViewId id);
		// The size a view is drawn at (clamped, and capped by its profile). A size that did not change does nothing; 0 in either dimension too.
		void resize_view(ViewId id, u32 width, u32 height);
		u32 view_count() const { return 1 + static_cast<u32>(m_ExtraViews.size()); }

		// Reads the image of a view back to the CPU: the copy is recorded the frame the view has drawn (or at once for a view that has drawn and
		// is not waiting to draw again), and the pixels reach the channel when the GPU has finished it. At most MAX_READBACKS_IN_FLIGHT copies are
		// recorded at a time; the rest wait in order. An empty ImageData answers a request that cannot be served (no such view, a failed target).
		void request_readback(ViewId view, u64 requestId, bool premultiplied);
		void set_readback_channel(ReadbackChannel* channel) { m_ReadbackChannel = channel; }
		static constexpr size_t MAX_READBACKS_IN_FLIGHT = 4;
		const std::vector<Scope<ViewState>>& extra_views() const { return m_ExtraViews; }
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

		// The index of the next frame to render: it picks the TAA jitter.
		u64 get_frame_index() const { return m_FrameIndex; }

	private:
		bool apply_vsync(u32 width, u32 height);
		void update_viewport();
		void update_view_target(ViewState& view);
		void write_view_stand_ins();
		void record_readbacks(const std::vector<ViewState*>& rendered);
		void mark_readbacks_submitted();
		void deliver_finished_readbacks();
		void answer_empty(u64 requestId);
		void drop_unsubmitted_readbacks();
		// The views to draw this frame: the active non-main ones (within the per-frame budget), then the main one.
		std::vector<ViewState*> pick_views_to_render();
		// The quality settings a view is drawn with: the project's, with the anti-aliasing its profile decides.
		RenderQuality effective_quality(const ViewState& view) const;
		bool resize_backend(u32 width, u32 height);
		PassInitializationDescription make_pass_description();
		FrameDescription make_frame_description(nvrhi::IFramebuffer* backbuffer, const RenderBatch& batch, ViewState& view);

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
		Scope<JobSystem> m_Jobs{};                 // background work of the renderer (the BRDF table, atmosphere builds)
		Scope<GpuResourceCache> m_Cache{};
		Scope<EnvironmentCache> m_Environments{};
		ViewState m_MainView{};                      // view 0: its scene, frame data and target (what get_scene() and friends return)
		std::vector<Scope<ViewState>> m_ExtraViews{}; // the views made for RenderViews, by creation order
		// 1x1 images the registry shows for a view that has not drawn yet (transparent) or whose target could not be made (magenta).
		nvrhi::TextureHandle m_ViewBlank{};
		nvrhi::TextureHandle m_ViewFailed{};
		bool m_StandInsWritten = false;

		struct ReadbackRequest
		{
			ViewId View = INVALID_VIEW;
			u64 Id = 0;
			bool Premultiplied = false;
			u64 QueuedFrame = 0; // the frame it was asked in: a request that cannot be served for MAX_READBACK_WAIT_FRAMES gets an empty answer
		};
		static constexpr u64 MAX_READBACK_WAIT_FRAMES = 600;
		struct ReadbackCopy
		{
			ReadbackRequest Request{};
			bool Transparent = false;
			u32 Width = 0;
			u32 Height = 0;
			nvrhi::StagingTextureHandle Staging{};
			nvrhi::EventQueryHandle Query{};
			bool Submitted = false;
		};
		std::deque<ReadbackRequest> m_ReadbackQueue{};
		std::vector<ReadbackCopy> m_ReadbackCopies{};
		ReadbackChannel* m_ReadbackChannel = nullptr;
		TextureRegistry m_Textures{};
		u32 m_RequestedViewportWidth = 0;
		u32 m_RequestedViewportHeight = 0;
		RenderQuality m_Quality{};
		bool m_SceneInViewport = false;
		bool m_ViewportFailed = false; // creating the target at the requested size failed: not tried again until the size changes
		nvrhi::CommandListHandle m_CommandList{};
		std::vector<Scope<IRenderPass>> m_Passes{};
		PassProfiler m_Profiler{};
		PickState m_Pick{};

		// One event query per submitted frame that has not been waited on yet, oldest first.
		std::deque<nvrhi::EventQueryHandle> m_FramesInFlight{};
		std::vector<nvrhi::EventQueryHandle> m_QueryPool{}; // queries ready for reuse

		bool m_PassesReady = false;
		bool m_Initialized = false;
	};
}
