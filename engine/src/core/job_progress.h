#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>

#include <math/math.h>

namespace wiwa
{
	// Shared between a background job (an import, a pack, loading a level's assets) and whoever shows it: the job reports
	// into it from its thread, a progress bar reads it from any thread, in the editor and in a game alike. Nothing here
	// blocks the reader except wait().
	class JobProgress
	{
	public:
		enum class State { Running, Succeeded, Failed };

		// Starts a new stage: done goes back to 0. total 0 means the amount of work is not known (an indeterminate bar).
		void begin(std::string stage, u32 total)
		{
			{
				std::lock_guard<std::mutex> lock(m_Mutex);
				m_Stage = std::move(stage);
				m_Item.clear();
			}
			m_Total = total;
			m_Done = 0;
		}

		// Names the stage that is running without touching the counts (a short last step of a job that is otherwise counted).
		void set_stage(std::string stage)
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Stage = std::move(stage);
		}

		// More work was found while running (a loaded asset names dependencies): the total grows, the bar must cope.
		void add_total(u32 count) { m_Total += count; }

		// One more item is done; item names the one that just finished (shown under the bar).
		void advance(std::string item = {})
		{
			if (!item.empty())
			{
				std::lock_guard<std::mutex> lock(m_Mutex);
				m_Item = std::move(item);
			}
			m_Done++;
		}

		void finish(bool succeeded)
		{
			{
				std::lock_guard<std::mutex> lock(m_Mutex);
				m_State = succeeded ? State::Succeeded : State::Failed;
			}
			m_Condition.notify_all();
		}

		// Asks the job to stop at the next item; it then finishes as Failed.
		void request_cancel() { m_Cancel = true; }
		bool cancel_requested() const { return m_Cancel; }

		State state() const { std::lock_guard<std::mutex> lock(m_Mutex); return m_State; }
		bool finished() const { return state() != State::Running; }
		bool succeeded() const { return state() == State::Succeeded; }

		u32 done() const { return m_Done; }
		u32 total() const { return m_Total; }
		// 0..1, or 0 while the total is unknown
		float fraction() const
		{
			const u32 total = m_Total;
			return total == 0 ? 0.0f : std::min(1.0f, static_cast<float>(m_Done.load()) / static_cast<float>(total));
		}

		std::string stage() const { std::lock_guard<std::mutex> lock(m_Mutex); return m_Stage; }
		std::string current_item() const { std::lock_guard<std::mutex> lock(m_Mutex); return m_Item; }

		// Blocks until the job has finished (for command-line runs and tests; the editor polls instead).
		void wait() const
		{
			std::unique_lock<std::mutex> lock(m_Mutex);
			m_Condition.wait(lock, [this]() { return m_State != State::Running; });
		}

	private:
		mutable std::mutex m_Mutex{};
		mutable std::condition_variable m_Condition{};
		State m_State = State::Running;
		std::string m_Stage{};
		std::string m_Item{};
		std::atomic<u32> m_Done{ 0 };
		std::atomic<u32> m_Total{ 0 };
		std::atomic<bool> m_Cancel{ false };
	};
}
