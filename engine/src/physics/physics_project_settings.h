#pragma once

#include <optional>

#include <assets/asset_guid.h>
#include <assets/asset_source.h>
#include <assets/wasset_io.h>

#include "physics_layers.h"

namespace wiwa
{
	// The key a pack stores the project's physics settings under (it is not an asset, so no real guid is this one). The editor writes
	// the settings to physics.wsettings and the packager puts them in assets.wpak; a game reads them from there.
	constexpr AssetGuid PHYSICS_SETTINGS_GUID{ ~0ull, ~0ull - 2 };

	constexpr u32 PHYSICS_SETTINGS_VERSION = 1;

	// A version, the layer names, the collision matrix. A blob that is not of this version or does not read to the end is refused.
	void archive(BinaryArchive& ar, PhysicsLayers& v);

	// The project's layers of a source; nullopt if the source has none (a Library, an older pack) or they are damaged.
	std::optional<PhysicsLayers> read_physics_layers(IAssetSource& source);
}
