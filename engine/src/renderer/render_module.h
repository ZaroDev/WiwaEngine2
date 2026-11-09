#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <assets/asset_guid.h>
#include <core/module.h>

#include "debug/debug_draw.h"
#include "imgui_layer.h"
#include "renderer.h"
#include "render_pick.h"
#include "render_quality.h"
#include "render_snapshot.h"
#include "render_thread.h"
#include "scene/scene_extractor.h"
#include "view/render_view.h"
#include "view/view_host_system.h"
#include "view/view_registry.h"

namespace wiwa
{
	class ImGuiRenderPass;
	class UiRenderPass;
	class RenderWorld;

	// What the main thread keeps for one offscreen view (views.md): the handle's settings and the extractor that mirrors the view's scene.
	struct ViewRecord
	{
		ViewId Id = INVALID_VIEW;
		ViewDesc Desc{};
		std::weak_ptr<Scene> Scene{};
		SceneExtractor Extractor{};
		RenderCamera Camera{};
		bool CameraDirty = false;
		bool RenderPending = false; // a OneShot view was asked to render: it is extracted on the next frame
		bool HadScene = false;
		bool WarnedGone = false;
		u32 Width = 1;
		u32 Height = 1;
	};

	// The main-thread face of the renderer: the only thing the rest of the engine talks to. It owns the render thread and
	// hands it one batch per frame; the Renderer, the backend and every GPU object live on that thread.
	//
	// It runs first in every hook (priority -50), so by the time other modules update, the frame it opens (the ImGui frame,
	// from Task 8 on) is ready for them to use.
	class RenderModule : public IModule
	{
	public:
		RenderModule() = default;
		virtual ~RenderModule() override = default;

		virtual void init() override;
		virtual void shutdown() override;

		// Opens the ImGui frame, so that every module's update() can build UI.
		virtual void pre_update(float deltaTime) override;
		virtual void render_update(float deltaTime) override;

		virtual UpdateFlags get_update_flags() const override { return UpdateFlags::UPDATE | UpdateFlags::UPDATE_RENDERER; }
		virtual std::string get_name() const override { return "Renderer"; }
		virtual int get_priority() const override { return -50; }

		// Main thread: queues a command for the next batch; it runs on the render thread against the render-side state.
		void submit(RenderCommand command) { m_PendingCommands.push_back(std::move(command)); }

		// The immediate-mode debug shapes (lines, boxes, spheres, ...) of the frame being built. Main thread only; they are drawn over the
		// scene a frame or two later, and the ones with a duration stay for that long.
		DebugDraw& get_debug_draw() { return m_DebugDraw; }

		// The latest stats the render thread published.
		RenderSnapshot get_snapshot() const { return m_Snapshot.read(); }

		// Turns vertical sync on or off; it takes effect on the render thread a frame or two later.
		void set_vsync(bool enabled);

		// The quality settings of the project (shadow reach and biases, vsync, copy queue). set_quality() applies them on the render
		// thread and saves them to render_quality.json next to the program.
		const RenderQuality& get_quality() const { return m_Quality; }
		void set_quality(const RenderQuality& quality);
		// The next frame starts TAA and motion blur without a history (the panel offers it when the image smears).
		void reset_temporal_history();
		// Shows the velocity of every pixel instead of the scene.
		void set_debug_velocity(bool enabled);

		// The size the scene is rendered at, in pixels; the Viewport panel asks for the size of its content region every frame. The render
		// thread makes (or remakes) the viewport target a frame or two later. The result is drawn by ImGui as the texture id below.
		void set_viewport_size(u32 width, u32 height);

		// The size of what the scene is drawn into, in pixels: the Viewport panel when there is one, else the window. 0 by 0 when there is nothing
		// to draw into (a minimized window).
		glm::vec2 get_canvas_size() const;

		// The game UI of the frame being built (UiModule::pre_render_update); it is sent with the next batch and drawn over the scene.
		void set_ui_draw_list(UiDrawList list) { m_PendingUi = std::move(list); }
		// False: the game UI is left out of the frames that are sent (the editor's Scene view). A game has it on, the default.
		void set_game_ui_visible(bool visible) { m_GameUiVisible = visible; }

		// True when the UI shows the scene in a panel (the editor's Viewport panel says so); false, the default, draws the scene over the
		// whole window (a game). Takes effect on the render thread a frame or two later.
		void set_scene_in_viewport(bool inViewport);

		// The entity to outline in the viewport (an empty one clears it), and how. Each sends a command only when it changed.
		void set_selection(std::optional<EntityGuid> guid);
		void set_outline_style(const SelectionOutlineStyle& style);

		// Asks which entity is drawn at a pixel of the viewport image ((0, 0) is the top left). The answer comes a frame or two later through
		// take_pick_result(); asking again before it arrives replaces the question. Returns the id the answer carries.
		u64 request_pick(u32 x, u32 y);
		// The answer to the latest request, once; an answer to an older one is dropped.
		std::optional<PickResult> take_pick_result() { return m_Picks.take(m_LastPickId); }

		// Draws the scene with this camera instead of the scene's primary one (the editor's fly camera); an empty one goes back to the
		// scene's camera. Sends a command only when it changed.
		void set_camera_override(const std::optional<RenderCamera>& camera);

		static constexpr u32 get_viewport_texture_id() { return VIEWPORT_TEXTURE_ID; }

		// Offscreen views: a scene of its own drawn into a texture, continuously or on request (RenderView). Returns an invalid handle (and logs
		// once) when MAX_VIEWS views are live. The view_* functions are what RenderView calls; use the handle.
		RenderView create_view(const ViewDesc& desc);
		void view_destroy(ViewId id);
		void view_set_scene(ViewId id, const Ref<Scene>& scene);
		void view_set_camera(ViewId id, const RenderCamera& camera);
		void view_resize(ViewId id, u32 width, u32 height);
		void view_request_render(ViewId id);
		void view_read_back(ViewId id, std::function<void(ImageData)> done, bool premultiplied);
		bool view_failed(ViewId id) const;
		// Names views so that a UI widget can show one (the ViewHost component sets them).
		ViewRegistry& get_view_names() { return m_ViewNames; }
		u32 view_count() const { return static_cast<u32>(m_ViewRecords.size()); }

		// The UI editor's canvas, drawn by the renderer like the viewport: every frame the render thread clears a texture of this size to the
		// background and draws the list into it (the list is in canvas pixels of list.CanvasSize). ImGui shows it as get_ui_preview_texture_id()
		// a frame or two later. Call it every frame the canvas is open; clear_ui_preview() when it is not.
		void set_ui_preview(UiDrawList list, u32 width, u32 height, const glm::vec4& background);
		void clear_ui_preview();
		static constexpr u32 get_ui_preview_texture_id() { return UI_PREVIEW_TEXTURE_ID; }

		// The id to draw a texture asset with in the UI (ImGui::Image), or 0 until the texture is on the GPU (and for good if it failed).
		// Asking starts the load; call it every frame the texture is shown. Main thread.
		u32 get_asset_texture_id(const AssetRef& texture);

		// A small RGBA8 image the UI can draw (the editor's thumbnails): the id for ImGui::Image is valid at once and draws nothing until the render
		// thread has uploaded the image, a frame or two later. release_ui_image() frees it. 0 for an image with no pixels. Main thread.
		u32 create_ui_image(const ImageData& image);
		void release_ui_image(u32 id);

		GraphicsApi get_api() const { return m_Description.Api; }
		// False if the renderer could not start or has failed since.
		bool is_ready() const { return m_Started && !m_Failed; }

	private:
		bool read_arguments();
		void apply_reloads();
		void publish_snapshot();
		void fail_and_quit(const char* reason);
		void run_render_test(RenderWorld& world);
		bool smoke_ready();

		RendererDescription m_Description{};
		ImGuiLayer m_ImGui{};
		RenderThread m_Thread{};
		SnapshotChannel m_Snapshot{};
		// Per-frame times of the render thread (frame to frame, and the CPU time of the frame), waiting for the main thread to hand them to PerfStats.
		std::mutex m_FrameSamplesMutex{};
		std::vector<std::pair<float, float>> m_FrameSamples{};
		std::chrono::steady_clock::time_point m_LastFrameStart{};
		Scope<Renderer> m_Renderer{}; // created, used and destroyed on the render thread
		ImGuiRenderPass* m_ImGuiPass = nullptr; // owned by the renderer; set on the render thread during start
		UiRenderPass* m_UiPass = nullptr;       // the same, for the game UI
		std::vector<RenderCommand> m_PendingCommands{};
		DebugDraw m_DebugDraw{};
		UiDrawList m_PendingUi{};
		bool m_GameUiVisible = true;
		SceneExtractor m_Extractor{}; // main thread: what the active scene changed since the last batch
		std::vector<Scope<ViewRecord>> m_ViewRecords{}; // main thread: the offscreen views, by creation
		ViewRegistry m_ViewNames{};
		ViewHostSystem m_ViewHosts{}; // the views of the ViewHost components of the active scene
		std::shared_ptr<bool> m_Alive = std::make_shared<bool>(true); // handles keep a weak reference: it expires at shutdown
		ViewId m_NextViewId = 1;
		bool m_WarnedViewLimit = false;
		ReadbackChannel m_Readbacks{};
		struct PendingReadback
		{
			ViewId View = INVALID_VIEW;
			std::function<void(ImageData)> Done{};
		};
		std::unordered_map<u64, PendingReadback> m_PendingReadbacks{}; // by request id; main thread
		u64 m_NextReadbackId = 1;
		std::vector<ViewId> m_FailedViews{}; // from the latest snapshot
		ViewRecord* find_view_record(ViewId id);

		// Assets the editor cooked again, noted by whichever thread reports it and taken by the render thread each frame.
		std::mutex m_ReloadMutex{};
		std::vector<AssetGuid> m_ReloadedAssets{};
		u32 m_ReloadListenerId = 0;
		u32 m_RequestedViewportWidth = 0; // main thread: what was last sent
		u32 m_RequestedViewportHeight = 0;
		bool m_SentSceneInViewport = false;
		RenderQuality m_Quality{};
		std::optional<RenderCamera> m_SentCameraOverride{};
		PickChannel m_Picks{};
		u64 m_LastPickId = 0;
		std::optional<EntityGuid> m_SentSelection{};
		SelectionOutlineStyle m_SentOutlineStyle{};
		// Texture assets shown by the UI: the id handed out per asset (main thread), and the ids the render thread has put in the
		// texture registry (under the mutex).
		static constexpr u32 FIRST_ASSET_TEXTURE_ID = 1u << 20;
		static constexpr u32 FIRST_UI_IMAGE_ID = 1u << 21;
		u32 m_NextUiImageId = FIRST_UI_IMAGE_ID;
		std::unordered_map<AssetGuid, u32> m_AssetTextureIds{};
		u32 m_NextAssetTextureId = FIRST_ASSET_TEXTURE_ID;
		std::mutex m_AssetTextureMutex{};
		std::unordered_set<u32> m_AssetTexturesReady{};
		u64 m_FrameIndex = 0;
		u64 m_SmokeCounted = 0;        // frames counted so far (they start once the scene of --scene has loaded)
		u64 m_SmokeAssetWait = 0;      // frames waited for the scene's assets to reach the GPU
		bool m_SmokeHasScene = false;  // --scene was given along with --render-smoke
		u64 m_SmokeFrames = 0; // --render-smoke: quit after this many frames, with exit code 1 if the GPU layers reported an error
		bool m_RenderTest = false;                // --render-test: run the render checks once the passes are ready, then quit
		u32 m_RenderTestWaitFrames = 0;           // render thread: frames waited for the passes
		std::atomic<int> m_RenderTestResult{ 0 }; // 0 running, 1 passed, 2 failed

		// Render thread only: the frame rate, averaged over a window.
		std::chrono::steady_clock::time_point m_StatsLast{};
		std::chrono::steady_clock::time_point m_StatsWindowStart{};
		u32 m_StatsFrames = 0;
		double m_Fps = 0.0;
		double m_FrameMs = 0.0;
		double m_UploadMs = 0.0;
		bool m_Started = false;
		std::atomic<bool> m_Failed{ false };
	};
}
