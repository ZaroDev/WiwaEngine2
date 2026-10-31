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
}
