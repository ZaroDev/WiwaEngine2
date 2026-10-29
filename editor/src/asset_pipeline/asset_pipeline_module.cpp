#include <editor_common.h>
#include "asset_pipeline_module.h"

#include "builtin_importers.h"

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	void AssetPipelineModule::init()
	{
		m_Database = CreateScope<AssetDatabase>("Assets", "Library");
		register_builtin_importers(*m_Database);
		m_Database->scan();
	}

	void AssetPipelineModule::shutdown()
	{
		m_Database.reset();
		WI_ASSET_LOG(Info, "Asset pipeline shutdown");
	}
}
