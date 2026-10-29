#include <pch.h>
#include "render_thread.h"

#include <future>

namespace wiwa
{
	bool RenderThread::start(std::function<bool()> init, std::function<void(RenderBatch&)> frame, std::function<void()> shutdown)
	{
		std::promise<bool> initialized;
		std::future<bool> initializedResult = initialized.get_future();

		m_Thread = std::thread([this, init = std::move(init), frame = std::move(frame), shutdown = std::move(shutdown), &initialized]()
		{
			const bool ok = init();
			initialized.set_value(ok);
			if (!ok)
			{
				return;
			}

			RenderBatch batch;
			while (m_Queue.pop(batch))
			{
				frame(batch);
			}

			shutdown();
		});

		if (!initializedResult.get())
		{
			m_Thread.join();
			return false;
		}

		m_Running = true;
		return true;
	}

	bool RenderThread::submit(RenderBatch&& batch)
	{
		return m_Queue.push(std::move(batch));
	}

	void RenderThread::stop()
	{
		m_Queue.close();
		if (m_Thread.joinable())
		{
			m_Thread.join();
		}
		m_Running = false;
	}
}
