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

namespace wiwa
{
	class ImGuiRenderPass;
	class RenderWorld;

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

		// The id to draw a texture asset with in the UI (ImGui::Image), or 0 until the texture is on the GPU (and for good if it failed).
		// Asking starts the load; call it every frame the texture is shown. Main thread.
		u32 get_asset_texture_id(const AssetRef& texture);

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
		Scope<Renderer> m_Renderer{}; // created, used and destroyed on the render thread
		ImGuiRenderPass* m_ImGuiPass = nullptr; // owned by the renderer; set on the render thread during start
		std::vector<RenderCommand> m_PendingCommands{};
		DebugDraw m_DebugDraw{};
		SceneExtractor m_Extractor{}; // main thread: what the active scene changed since the last batch

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
