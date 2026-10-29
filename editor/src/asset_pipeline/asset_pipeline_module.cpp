#include <editor_common.h>
#include "asset_pipeline_module.h"

#include <assets/asset_manager.h>
#include <core/engine.h>

#include "builtin_importers.h"

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	void AssetPipelineModule::init()
	{
		// Assets/ is the user's; engine/ holds the engine's own assets (its shaders, ...) and is cooked into Library/engine.
		m_Database = CreateScope<AssetDatabase>("Assets", "Library");
		m_Database->add_root("engine", "Library/engine");
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
			Packager::pack(*database, outDirectory, progress.get());
		});
		return progress;
	}

	void AssetPipelineModule::shutdown()
	{
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
