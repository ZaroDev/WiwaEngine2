#pragma once

#if !WI_SHIPPING

namespace wiwa
{
	class AssetDatabase;

	// Registers the importers the engine ships with (textures, models, environments, LUTs, shaders).
	void register_builtin_importers(AssetDatabase& database);
}

#endif
