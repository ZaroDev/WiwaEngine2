#pragma once

#include <atomic>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <vector>

#include <assets/asset_guid.h>

namespace wiwa
{
	struct ReferenceHit
	{
		std::filesystem::path File{};
		int Count = 0; // how many times the guid is written in the file
	};

	// Finds where an asset is used: reads the source files that hold asset references (scenes, prefabs, materials, UI panels, input maps)
	// under the given folders on a worker thread and counts the places that write the asset's guid. It only reads. Main thread starts it and
	// asks for the result; the worker owns nothing else.
	class ReferenceFinder
	{
	public:
		ReferenceFinder() = default;
		~ReferenceFinder();
		ReferenceFinder(const ReferenceFinder&) = delete;
		ReferenceFinder& operator=(const ReferenceFinder&) = delete;

		// Starts again (a search in progress is given up).
		// fileName: the source file name of the asset; materials (.wmat) name their textures and shaders by relative path, so they are searched
		// for the name as well as the guid.
		// redirectedFrom: the models whose import redirected something to guid (AssetDatabase::redirect_sources): a file that still names such a model
		// may reach the asset through the redirect, so it counts as a reference too.
		void start(AssetGuid guid, std::string fileName, std::vector<std::filesystem::path> roots, std::vector<AssetGuid> redirectedFrom = {});

		bool started() const { return m_State != nullptr; }
		bool finished() const { return m_State != nullptr && m_State->Finished.load(); }
		// 0 to 1.
		float progress() const;
		// What has been found so far, files with the most uses first.
		std::vector<ReferenceHit> hits() const;
		AssetGuid target() const { return m_Target; }

	private:
		struct State
		{
			std::atomic<bool> Cancel{ false };
			std::atomic<bool> Finished{ false };
			std::atomic<size_t> Done{ 0 };
			std::atomic<size_t> Total{ 0 };
			mutable std::mutex Mutex{};
			std::vector<ReferenceHit> Hits{};
		};

		void stop();

		std::shared_ptr<State> m_State{};
		std::future<void> m_Task{};
		AssetGuid m_Target{};
	};
}
