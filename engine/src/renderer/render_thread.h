#pragma once

#include <atomic>
#include <functional>
#include <thread>

#include "render_queue.h"

namespace wiwa
{
	// The thread that owns the GPU. It runs init once, then frame for every batch the main thread submits, then shutdown.
	class RenderThread
	{
	public:
		RenderThread() : m_Queue(RENDER_QUEUE_DEPTH) {}
		~RenderThread() { stop(); }

		RenderThread(const RenderThread&) = delete;
		RenderThread& operator=(const RenderThread&) = delete;

		// Starts the thread and waits for init (which runs on it) to finish. Returns init's result; on false the thread
		// has already ended and shutdown is not called (init cleans up after itself).
		bool start(std::function<bool()> init, std::function<void(RenderBatch&)> frame, std::function<void()> shutdown);

		// Main thread: hands a frame over; blocks while the queue is full. Returns false once the thread is stopping.
		bool submit(RenderBatch&& batch);

		// Closes the queue (waking a blocked submit), lets the thread run shutdown, and joins it. Safe to call again.
		void stop();

		bool is_running() const { return m_Running; }

	private:
		RenderQueue m_Queue;
		std::thread m_Thread{};
		std::atomic<bool> m_Running{ false };
	};
}
