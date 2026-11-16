#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <vector>

#include <math/math.h>

#include "debug/debug_draw.h"
#include <ui/ui_draw_list.h>

#include "imgui_draw_data.h"

namespace wiwa
{
	class RenderWorld;

	// How many frames the main thread may be ahead of the render thread.
	constexpr u32 RENDER_QUEUE_DEPTH = 2;

	// Something the main thread asks the render thread to do, run on the render thread against its own state.
	using RenderCommand = std::function<void(RenderWorld&)>;

	// One frame of work: what happened on the main thread since the last batch, sealed and handed over.
	struct RenderBatch
	{
		u64 FrameIndex = 0;
		float DeltaSeconds = 0.0f; // since the previous batch
		u32 Width = 0;
		u32 Height = 0;
		bool Minimized = false; // nothing to show: the render thread does not draw this frame
		std::vector<RenderCommand> Commands{};
		OwnedDrawData ImGui{};
		DebugDrawData Debug{}; // the shapes of this frame (DebugDraw::seal)
		UiDrawList Ui{};       // the game UI of this frame (UiModule), drawn over the scene
	};

	// A bounded hand-off from one producer (the main thread) to one consumer (the render thread).
	class RenderQueue
	{
	public:
		explicit RenderQueue(u32 depth) : m_Depth(depth) {}

		// Blocks while the queue is full. Returns false (the batch is dropped) once the queue is closed.
		bool push(RenderBatch&& batch)
		{
			std::unique_lock<std::mutex> lock(m_Mutex);
			m_NotFull.wait(lock, [this]() { return m_Closed || m_Batches.size() < m_Depth; });
			if (m_Closed)
			{
				return false;
			}

			m_Batches.push_back(std::move(batch));
			m_NotEmpty.notify_one();
			return true;
		}

		// Blocks until a batch is available. Returns false as soon as the queue is closed, whatever is still queued is dropped.
		bool pop(RenderBatch& batch)
		{
			std::unique_lock<std::mutex> lock(m_Mutex);
			m_NotEmpty.wait(lock, [this]() { return m_Closed || !m_Batches.empty(); });
			if (m_Closed)
			{
				return false;
			}

			batch = std::move(m_Batches.front());
			m_Batches.pop_front();
			m_NotFull.notify_one();
			return true;
		}

		// Wakes everyone who is blocked; from now on push and pop return false.
		void close()
		{
			{
				std::lock_guard<std::mutex> lock(m_Mutex);
				m_Closed = true;
			}
			m_NotFull.notify_all();
			m_NotEmpty.notify_all();
		}

		u32 size() const
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			return static_cast<u32>(m_Batches.size());
		}

	private:
		u32 m_Depth;
		mutable std::mutex m_Mutex{};
		std::condition_variable m_NotFull{};
		std::condition_variable m_NotEmpty{};
		std::deque<RenderBatch> m_Batches{};
		bool m_Closed = false;
	};
}
