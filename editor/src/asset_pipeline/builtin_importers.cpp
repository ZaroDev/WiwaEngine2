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
		database.register_importer(create_script_importer());
		database.register_importer(create_material_importer());
		database.register_importer(create_surface_shader_importer());
		database.register_importer(create_prefab_importer());
		database.register_importer(create_ui_panel_importer());
		database.register_importer(create_font_importer());
		database.register_importer(create_physics_material_importer());
		database.register_importer(create_input_map_importer());
		database.register_importer(create_skeleton_file_importer());
		database.register_importer(create_clip_file_importer());
		database.register_importer(create_mesh_file_importer());
		database.register_importer(create_graph_file_importer());
		database.register_importer(create_particle_file_importer());
		database.register_importer(create_sound_bank_importer());
		database.register_importer(create_audio_clip_importer());
	}
}
