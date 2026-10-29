#include <pch.h>
#include "builtin_importers.h"

#if !WI_SHIPPING

#include "asset_database.h"
#include "importers/importers.h"

namespace wiwa
{
	void register_builtin_importers(AssetDatabase& database)
	{
		database.register_importer(create_texture_importer());
		database.register_importer(create_environment_importer());
		database.register_importer(create_lut_importer());
		database.register_importer(create_shader_importer());
		database.register_importer(create_model_importer());
	}
}

#endif
