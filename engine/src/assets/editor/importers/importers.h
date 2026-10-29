#pragma once

#if !WI_SHIPPING

#include <core/common.h>
#include "assets/editor/importer.h"

namespace wiwa
{
	Scope<IImporter> create_texture_importer();     // .png .jpg .jpeg .tga .bmp
	Scope<IImporter> create_environment_importer(); // .hdr
	Scope<IImporter> create_lut_importer();         // .cube
	Scope<IImporter> create_shader_importer();      // .hlsl .hlsli
	Scope<IImporter> create_model_importer();       // .gltf .glb .fbx
}

#endif
