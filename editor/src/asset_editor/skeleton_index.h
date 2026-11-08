#pragma once

#include <atomic>
#include <functional>
#include <future>
#include <memory>
#include <unordered_map>

#include "skeleton_dependents.h"

namespace wiwa
{
	class AssetDatabase;

	// Which meshes and clips name each skeleton, kept up to date in the background: the Asset Editor's preview and the thumbnails ask it for the
	// mesh a skeleton is drawn with and the clips it can play. A scan reads every .wmesh/.wanim, so it runs on a worker; invalidate() (any thread,
	// the catalog listener calls it) makes the next update() scan again. Main thread, except invalidate().
	class SkeletonIndex
	{
	public:
		explicit SkeletonIndex(const AssetDatabase& database) : m_Database(database) {}
		~SkeletonIndex();
		SkeletonIndex(const SkeletonIndex&) = delete;
		SkeletonIndex& operator=(const SkeletonIndex&) = delete;

		void invalidate() { *m_Dirty = true; }
		// A function that does invalidate() and stays safe after the index is gone (the catalog listener outlives it at shutdown).
		std::function<void()> invalidator() const { std::shared_ptr<std::atomic<bool>> dirty = m_Dirty; return [dirty]() { *dirty = true; }; }
		// Takes a finished scan, starts the next one when the files changed.
		void update();

		// False until the first scan finished.
		bool ready() const { return m_Ready; }
		// Increases whenever a scan result was taken: a cache keyed by it knows when to look again.
		u32 generation() const { return m_Generation; }
		SkeletonUsers users_of(AssetGuid skeleton) const;

	private:
		const AssetDatabase& m_Database;
		std::unordered_map<AssetGuid, SkeletonUsers> m_Index{};
		std::future<std::unordered_map<AssetGuid, SkeletonUsers>> m_Task{};
		std::shared_ptr<std::atomic<bool>> m_Dirty = std::make_shared<std::atomic<bool>>(true);
		std::atomic<bool> m_Cancel{ false };
		bool m_Ready = false;
		u32 m_Generation = 0;
	};
}
