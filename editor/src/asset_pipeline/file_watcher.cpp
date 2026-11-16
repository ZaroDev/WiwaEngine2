#include <editor_common.h>
#include "file_watcher.h"

#define NOMINMAX
#include <FileWatch.hpp>

#include <share.h>

#include <assets/asset_log.h>

namespace wiwa
{
	namespace
	{
		constexpr auto DEBOUNCE = std::chrono::milliseconds(300);
		constexpr auto RETRY_DELAY = std::chrono::milliseconds(200);
		constexpr u32 MAX_ATTEMPTS = 5;

		// A file another program is still writing cannot be opened for reading with write-sharing denied.
		bool is_settled(const std::filesystem::path& path)
		{
			FILE* file = _wfsopen(path.c_str(), L"rb", _SH_DENYWR);
			if (file == nullptr)
			{
				return false;
			}
			fclose(file);
			return true;
		}

		bool is_ignored(const std::filesystem::path& relative)
		{
			for (const std::filesystem::path& part : relative)
			{
				const std::string name = part.string();
				if (name.starts_with(".") || name == "Library")
				{
					return true;
				}
			}

			const std::string extension = relative.extension().string();
			return extension == ".tmp" || extension == ".bad";
		}
	}

	struct FileWatcher::Impl
	{
		std::unique_ptr<filewatch::FileWatch<std::wstring>> Watch;
	};

	FileWatcher::FileWatcher(std::filesystem::path root, std::function<void(const std::filesystem::path&)> onChanged)
		: m_Root(std::move(root)), m_OnChanged(std::move(onChanged)), m_Impl(CreateScope<Impl>())
	{
		std::error_code error;
		std::filesystem::create_directories(m_Root, error);

		m_Impl->Watch = std::make_unique<filewatch::FileWatch<std::wstring>>(
			std::filesystem::absolute(m_Root).wstring(),
			[this](const std::wstring& file, const filewatch::Event event)
			{
				if (event == filewatch::Event::added || event == filewatch::Event::modified || event == filewatch::Event::renamed_new)
				{
					on_event(std::filesystem::path(file));
				}
			});

		m_Thread = std::thread([this]() { run(); });
	}

	FileWatcher::~FileWatcher()
	{
		m_Impl->Watch.reset(); // no more events

		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_Stopping = true;
		}
		m_Condition.notify_all();
		m_Thread.join();
	}

	void FileWatcher::on_event(const std::filesystem::path& relative)
	{
		if (is_ignored(relative))
		{
			return;
		}

		std::filesystem::path source = relative;
		if (source.extension() == ".meta")
		{
			source.replace_extension(); // an edited .meta means its source has to be looked at again
		}

		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			Pending& pending = m_Pending[source];
			pending.LastEvent = std::chrono::steady_clock::now();
			pending.Attempts = 0;
		}
		m_Condition.notify_all();
	}

	void FileWatcher::run()
	{
		std::unique_lock<std::mutex> lock(m_Mutex);
		while (!m_Stopping)
		{
			m_Condition.wait_for(lock, std::chrono::milliseconds(100));
			if (m_Stopping)
			{
				break;
			}

			const auto now = std::chrono::steady_clock::now();
			std::vector<std::filesystem::path> due;
			for (auto it = m_Pending.begin(); it != m_Pending.end();)
			{
				if (now - it->second.LastEvent < DEBOUNCE)
				{
					++it;
					continue;
				}

				const std::filesystem::path full = m_Root / it->first;
				std::error_code error;
				if (!std::filesystem::is_regular_file(full, error))
				{
					it = m_Pending.erase(it); // deleted again, or a folder
					continue;
				}

				if (is_settled(full))
				{
					due.push_back(full);
					it = m_Pending.erase(it);
				}
				else if (++it->second.Attempts >= MAX_ATTEMPTS)
				{
					WI_ASSET_LOG(Warning, "{0} is still being written, giving up for now", full.generic_string());
					it = m_Pending.erase(it);
				}
				else
				{
					it->second.LastEvent = now - DEBOUNCE + RETRY_DELAY; // try again in RETRY_DELAY
					++it;
				}
			}

			lock.unlock();
			for (const std::filesystem::path& path : due)
			{
				m_OnChanged(path);
			}
			lock.lock();
		}
	}
}
