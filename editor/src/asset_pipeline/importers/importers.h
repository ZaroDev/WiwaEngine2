#pragma once

#include <core/common.h>
#include "../importer.h"

namespace wiwa
{
	Scope<IImporter> create_texture_importer();     // .png .jpg .jpeg .tga .bmp
	Scope<IImporter> create_environment_importer(); // .hdr
	Scope<IImporter> create_lut_importer();         // .cube
	Scope<IImporter> create_shader_importer();      // .hlsl .hlsli
	Scope<IImporter> create_model_importer();       // .gltf .glb .fbx
	Scope<IImporter> create_scene_importer();       // .wscene
	Scope<IImporter> create_script_importer();      // .lua
	Scope<IImporter> create_material_importer();    // .wmat
	Scope<IImporter> create_surface_shader_importer(); // .wshader
	Scope<IImporter> create_prefab_importer();      // .wprefab
	Scope<IImporter> create_ui_panel_importer();    // .wuipanel
	Scope<IImporter> create_font_importer();        // .ttf .otf
	Scope<IImporter> create_physics_material_importer(); // .wphysmat
	Scope<IImporter> create_input_map_importer(); // .winput
	Scope<IImporter> create_skeleton_file_importer(); // .wskel
	Scope<IImporter> create_clip_file_importer();     // .wanim
	Scope<IImporter> create_mesh_file_importer();     // .wmesh
	Scope<IImporter> create_graph_file_importer();    // .wgraph
	Scope<IImporter> create_particle_file_importer(); // .wfx
	Scope<IImporter> create_sound_bank_importer();    // .bnk
	Scope<IImporter> create_audio_clip_importer();    // .wav .mp3 .flac .ogg
	Scope<IImporter> create_nav_mesh_importer();      // .wnav
}
