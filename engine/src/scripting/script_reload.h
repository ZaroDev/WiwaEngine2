#pragma once

#include <span>

#include <assets/asset_catalog.h>

namespace wiwa
{
	// Whether the asset is a Lua script: the catalog lists it under a path that ends in .lua. A cooked asset that was re-imported is
	// reloaded as a script when it is one, even if the runtime never got as far as running it (it failed the first time).
	bool is_script_asset(std::span<const AssetCatalog::Entry> catalog, AssetGuid guid);
}
