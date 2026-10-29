#pragma once

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

#include <math/math.h>

namespace wiwa
{
	// A small thread pool. submit() queues a function on a worker thread and returns a future for its
	// result. Jobs must not touch the GPU (NVRHI device objects belong to the render thread).
	class JobSystem
	{
	public:
		// 0 = all cores but one (at least one worker).
		explicit JobSystem(u32 workerCount = 0);

		// Waits for the jobs that are running; jobs still queued are discarded.
		~JobSystem();

		JobSystem(const JobSystem&) = delete;
		JobSystem& operator=(const JobSystem&) = delete;

		template<typename Function>
		auto submit(Function&& function) -> std::future<std::invoke_result_t<std::decay_t<Function>>>
		{
			using Result = std::invoke_result_t<std::decay_t<Function>>;

			auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<Function>(function));
			std::future<Result> future = task->get_future();

			{
				std::lock_guard<std::mutex> lock(m_Mutex);
				m_Queue.emplace([task = std::move(task)]() { (*task)(); });
			}
			m_Condition.notify_one();

			return future;
		}

		u32 get_worker_count() const { return static_cast<u32>(m_Workers.size()); }

	private:
		void worker_loop();

		std::vector<std::thread> m_Workers{};
		std::queue<std::function<void()>> m_Queue{};
		std::mutex m_Mutex{};
		std::condition_variable m_Condition{};
		bool m_Stopping{};
	};
}
