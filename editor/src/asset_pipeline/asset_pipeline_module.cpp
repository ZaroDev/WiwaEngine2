#include <editor_common.h>
#include "asset_pipeline_module.h"

#include <assets/asset_manager.h>
#include <core/engine.h>

#include <scene/reflection/component_registry.h>

#include "builtin_importers.h"
#include "lua_stubs.h"

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	void AssetPipelineModule::init()
	{
		// Assets/ is the user's; engine/ holds the engine's own assets (its shaders, ...) and is cooked into Library/engine.
		m_Database = CreateScope<AssetDatabase>("assets", "library");
		m_Database->add_root("engine", "library/engine");
		register_builtin_importers(*m_Database);
		// In the background: the editor opens at once and the import reports through the database's progress object.
		m_Database->scan_async();

		// Re-cooked assets: tell the AssetManager so loaded copies can be refreshed.
		// (Looked up once here: the listener runs on worker threads, which must not touch the module map.)
		AssetManager* manager = Engine::get().get_module<AssetManager>();
		m_Database->add_reimport_listener([manager](AssetGuid guid)
		{
			manager->notify_reimported(guid);
		});

		// Live re-import while the editor runs.
		AssetDatabase* database = m_Database.get();
		for (const AssetDatabase::SourceRoot& root : database->get_roots())
		{
			m_Watchers.push_back(CreateScope<FileWatcher>(root.Source, [database](const std::filesystem::path& file)
			{
				database->reimport(file);
			}));
		}

		// LuaLS (the Lua extension of VS Code) finds the stubs through this file
		ensure_luarc(".luarc.json");
		m_StubThread = std::thread([this]() { watch_lua_stubs(); });
	}

	void AssetPipelineModule::watch_lua_stubs()
	{
		const std::vector<const ComponentInfo*>* last = nullptr; // every change of the registry publishes a new snapshot
		std::unique_lock<std::mutex> lock(m_StubMutex);
		while (!m_StubStop)
		{
			const std::vector<const ComponentInfo*>* current = &ComponentRegistry::get().all();
			if (current != last)
			{
				last = current;
				lock.unlock();
				write_component_stubs("library/lua/generated.lua");
				lock.lock();
			}
			m_StubWake.wait_for(lock, std::chrono::milliseconds(500), [this]() { return m_StubStop; });
		}
	}

	Ref<JobProgress> AssetPipelineModule::pack_async(const std::filesystem::path& outDirectory)
	{
		if (m_PackProgress && !m_PackProgress->finished())
		{
			return m_PackProgress;
		}

		if (m_PackThread.joinable())
		{
			m_PackThread.join(); // the previous pack has finished
		}

		Ref<JobProgress> progress = CreateRef<JobProgress>();
		m_PackProgress = progress;
		AssetDatabase* database = m_Database.get();
		m_PackThread = std::thread([database, outDirectory, progress]()
		{
			// What goes into the pack is decided by build_settings.json; without one everything is packed. A file that cannot be
			// read stops the pack: packing everything instead would ship what the settings meant to leave out.
			std::string settingsError;
			std::optional<BuildSettings> settings = BuildSettings::load(BUILD_SETTINGS_FILE, settingsError);
			if (!settings && !settingsError.empty())
			{
				WI_ASSET_LOG(Error, "Packing failed: {0} cannot be read: {1}", BUILD_SETTINGS_FILE, settingsError);
				progress->begin("Reading the build settings", 0);
				progress->finish(false);
				return;
			}
			Packager::pack(*database, outDirectory, progress.get(), settings ? &*settings : nullptr);
		});
		return progress;
	}

	void AssetPipelineModule::shutdown()
	{
		{
			std::lock_guard<std::mutex> lock(m_StubMutex);
			m_StubStop = true;
		}
		m_StubWake.notify_all();
		if (m_StubThread.joinable())
		{
			m_StubThread.join();
		}

		// Running jobs are asked to stop and joined before the database they use goes away.
		if (m_PackProgress)
		{
			m_PackProgress->request_cancel();
		}
		if (m_PackThread.joinable())
		{
			m_PackThread.join();
		}

		m_Watchers.clear(); // before the database they call into
		m_Database.reset();
		WI_ASSET_LOG(Info, "Asset pipeline shutdown");
	}
}
