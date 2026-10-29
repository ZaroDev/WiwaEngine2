#include <pch.h>
#include "job_system.h"

namespace wiwa
{
	JobSystem::JobSystem(u32 workerCount)
	{
		if (workerCount == 0)
		{
			const u32 cores = std::thread::hardware_concurrency();
			workerCount = (cores > 2) ? cores - 1 : 1;
		}

		m_Workers.reserve(workerCount);
		for (u32 i = 0; i < workerCount; i++)
		{
			m_Workers.emplace_back([this]() { worker_loop(); });
		}
	}

	JobSystem::~JobSystem()
	{
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Stopping = true;
		}
		m_Condition.notify_all();

		for (std::thread& worker : m_Workers)
		{
			worker.join();
		}
	}

	void JobSystem::worker_loop()
	{
		while (true)
		{
			std::function<void()> job;

			{
				std::unique_lock<std::mutex> lock(m_Mutex);
				m_Condition.wait(lock, [this]() { return m_Stopping || !m_Queue.empty(); });

				if (m_Stopping)
				{
					return; // queued jobs are dropped
				}

				job = std::move(m_Queue.front());
				m_Queue.pop();
			}

			job();
		}
	}
}
