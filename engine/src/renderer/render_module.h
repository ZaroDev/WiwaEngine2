#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>

#include <assets/asset_guid.h>
#include <core/module.h>

#include "imgui_layer.h"
#include "renderer.h"
#include "render_snapshot.h"
#include "render_thread.h"

namespace wiwa
{
	class ImGuiRenderPass;

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

		// The latest stats the render thread published.
		RenderSnapshot get_snapshot() const { return m_Snapshot.read(); }

		// Turns vertical sync on or off; it takes effect on the render thread a frame or two later.
		void set_vsync(bool enabled);

		GraphicsApi get_api() const { return m_Description.Api; }
		// False if the renderer could not start or has failed since.
		bool is_ready() const { return m_Started && !m_Failed; }

	private:
		bool read_arguments();
		void apply_reloads();
		void publish_snapshot();
		void fail_and_quit(const char* reason);

		RendererDescription m_Description{};
		ImGuiLayer m_ImGui{};
		RenderThread m_Thread{};
		SnapshotChannel m_Snapshot{};
		Scope<Renderer> m_Renderer{}; // created, used and destroyed on the render thread
		ImGuiRenderPass* m_ImGuiPass = nullptr; // owned by the renderer; set on the render thread during start
		std::vector<RenderCommand> m_PendingCommands{};

		// Assets the editor cooked again, noted by whichever thread reports it and taken by the render thread each frame.
		std::mutex m_ReloadMutex{};
		std::vector<AssetGuid> m_ReloadedAssets{};
		u32 m_ReloadListenerId = 0;
		u64 m_FrameIndex = 0;
		u64 m_SmokeFrames = 0; // --render-smoke: quit after this many frames, with exit code 1 if the GPU layers reported an error

		// Render thread only: the frame rate, averaged over a window.
		std::chrono::steady_clock::time_point m_StatsLast{};
		std::chrono::steady_clock::time_point m_StatsWindowStart{};
		u32 m_StatsFrames = 0;
		double m_Fps = 0.0;
		double m_FrameMs = 0.0;
		bool m_Started = false;
		std::atomic<bool> m_Failed{ false };
	};
}
