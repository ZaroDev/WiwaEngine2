#pragma once

#include <atomic>
#include <vector>

#include <core/module.h>

#include "renderer.h"
#include "render_snapshot.h"
#include "render_thread.h"

namespace wiwa
{
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

		virtual void render_update(float deltaTime) override;

		virtual UpdateFlags get_update_flags() const override { return UpdateFlags::UPDATE | UpdateFlags::UPDATE_RENDERER; }
		virtual std::string get_name() const override { return "Renderer"; }
		virtual int get_priority() const override { return -50; }

		// Main thread: queues a command for the next batch; it runs on the render thread against the render-side state.
		void submit(RenderCommand command) { m_PendingCommands.push_back(std::move(command)); }

		// The latest stats the render thread published.
		RenderSnapshot get_snapshot() const { return m_Snapshot.read(); }

		GraphicsApi get_api() const { return m_Description.Api; }
		// False if the renderer could not start or has failed since.
		bool is_ready() const { return m_Started && !m_Failed; }

	private:
		bool read_arguments();
		void fail_and_quit(const char* reason);

		RendererDescription m_Description{};
		RenderThread m_Thread{};
		SnapshotChannel m_Snapshot{};
		Scope<Renderer> m_Renderer{}; // created, used and destroyed on the render thread
		std::vector<RenderCommand> m_PendingCommands{};
		u64 m_FrameIndex = 0;
		bool m_Started = false;
		std::atomic<bool> m_Failed{ false };
	};
}
