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
		m_Database->scan();

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

	void AssetPipelineModule::shutdown()
	{
		m_Watchers.clear(); // before the database they call into
		m_Database.reset();
		WI_ASSET_LOG(Info, "Asset pipeline shutdown");
	}
}
