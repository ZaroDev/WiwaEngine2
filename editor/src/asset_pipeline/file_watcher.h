#pragma once

#include <chrono>
#include <condition_variable>
#include <thread>

namespace wiwa
{
	// Watches a folder tree and reports files that were added or changed, once they have settled: events are debounced
	// (a save usually fires several) and a file another program still has open for writing is retried a few times.
	// Edits of a "<file>.meta" are reported as the file they belong to. The callback runs on the watcher's own thread.
	class FileWatcher
	{
	public:
		FileWatcher(std::filesystem::path root, std::function<void(const std::filesystem::path&)> onChanged);
		~FileWatcher();

		FileWatcher(const FileWatcher&) = delete;
		FileWatcher& operator=(const FileWatcher&) = delete;

	private:
		struct Impl;
		struct Pending
		{
			std::chrono::steady_clock::time_point LastEvent{};
			u32 Attempts = 0;
		};

		void on_event(const std::filesystem::path& relative);
		void run();

		std::filesystem::path m_Root;
		std::function<void(const std::filesystem::path&)> m_OnChanged;

		std::mutex m_Mutex{};
		std::condition_variable m_Condition{};
		std::map<std::filesystem::path, Pending> m_Pending{};
		bool m_Stopping = false;

		Scope<Impl> m_Impl{};
		std::thread m_Thread{};
	};
}
