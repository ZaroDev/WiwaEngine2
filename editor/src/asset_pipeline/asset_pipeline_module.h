#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>

#include <core/module.h>

#include "asset_database.h"
#include "file_watcher.h"
#include "packager.h"

namespace wiwa
{
	// Part of the editor project, not of the engine: owns the AssetDatabase that imports Assets/ into Library/.
	// A game built on the engine has no such module and just reads the cooked files (or the pack) through the AssetManager.
	class AssetPipelineModule : public IModule
	{
	public:
		AssetPipelineModule() = default;
		virtual ~AssetPipelineModule() override = default;

		virtual void init() override;
		virtual void shutdown() override;

		virtual UpdateFlags get_update_flags() const override { return UpdateFlags::NONE; }
		virtual std::string get_name() const override { return "AssetPipeline"; }
		virtual int get_priority() const override { return -90; }

		AssetDatabase& get_database() { return *m_Database; }

		// Imports what is new or changed in the background; the progress object is for a progress bar.
		Ref<JobProgress> scan_async() { return m_Database->scan_async(); }

		// Packs Library/ into <outDirectory>/assets.wpak in the background. One pack at a time: while one is running, its
		// progress is returned.
		Ref<JobProgress> pack_async(const std::filesystem::path& outDirectory);

		// The pack in progress (or the last one), null if none was started.
		Ref<JobProgress> get_pack_progress() const { return m_PackProgress; }

	private:
		// Keeps library/lua/generated.lua (the LuaLS stub of the registered components) current: written at start, and again whenever
		// a component is registered or removed (a script imported, loaded or reloaded), noticed by polling the registry's snapshot.
		void watch_lua_stubs();

		Scope<AssetDatabase> m_Database{};
		std::thread m_StubThread{};
		std::mutex m_StubMutex{};
		std::condition_variable m_StubWake{};
		bool m_StubStop = false;
		std::vector<Scope<FileWatcher>> m_Watchers{};
		Ref<JobProgress> m_PackProgress{};
		std::thread m_PackThread{};
	};
}
