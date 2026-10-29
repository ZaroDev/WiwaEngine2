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
		m_Database = CreateScope<AssetDatabase>("Assets", "Library");
		register_builtin_importers(*m_Database);
		m_Database->scan();

		// Re-cooked assets: tell the AssetManager so loaded copies can be refreshed.
		m_Database->add_reimport_listener([](AssetGuid guid)
		{
			Engine::get().get_module<AssetManager>()->notify_reimported(guid);
		});

		// Live re-import while the editor runs.
		AssetDatabase* database = m_Database.get();
		m_Watcher = CreateScope<FileWatcher>(database->get_assets_root(), [database](const std::filesystem::path& file)
		{
			database->reimport(file);
		});
	}

	void AssetPipelineModule::shutdown()
	{
		m_Watcher.reset(); // before the database it calls into
		m_Database.reset();
		WI_ASSET_LOG(Info, "Asset pipeline shutdown");
	}
}
