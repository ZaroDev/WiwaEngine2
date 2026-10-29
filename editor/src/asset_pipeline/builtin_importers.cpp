#include <editor_common.h>
#include "builtin_importers.h"

#include <scene/scene_components.h>

#include "asset_database.h"
#include "importers/importers.h"

namespace wiwa
{
	void register_builtin_importers(AssetDatabase& database)
	{
		register_scene_components(); // the scene importer checks the fields of a scene file against them
		database.register_importer(create_texture_importer());
		database.register_importer(create_environment_importer());
		database.register_importer(create_lut_importer());
		database.register_importer(create_shader_importer());
		database.register_importer(create_model_importer());
		database.register_importer(create_scene_importer());
	}
}
