#pragma once

#include <optional>

#include <assets/asset_guid.h>
#include <assets/asset_source.h>
#include <assets/wasset_io.h>

#include "nav_types.h"

namespace wiwa
{
	// The key a pack stores the project navigation settings under (it is not an asset, so no real guid is this one). The editor writes them to
	// nav.wsettings and the packager puts them in assets.wpak; a game reads them from there.
	constexpr AssetGuid NAV_SETTINGS_GUID{ ~0ull, ~0ull - 6 };

	constexpr u32 NAV_SETTINGS_VERSION = 2; // 1 held the costs of the eight fixed areas only; it is still read
	constexpr size_t NAV_AREA_NAME_MAX = 32;
	constexpr float NAV_AREA_COST_MAX = 1000.0f;

	// Every cost is kept inside 1..NAV_AREA_COST_MAX (a cost below 1 would make Detour's heuristic inadmissible); a value that is not a number becomes 1.
	// Ids 0 and 1 keep their names; a name is trimmed, cut to NAV_AREA_NAME_MAX, and an empty one, or one another area already has (the lower id keeps it),
	// makes the slot unused. An unused slot has cost 1.
	NavSettings sanitized(NavSettings settings);

	// Whether a name can be given to an area of the slot range as it is (not empty, short enough, no control character, no other area has it).
	bool nav_area_name_ok(const NavSettings& settings, u32 id, const std::string& name);

	// A version, then the costs, then the names (version 2). A blob of another version, with a cost that is not a finite number >= 1, a name that is not
	// valid, or that does not read to the end is refused.
	void archive(BinaryArchive& ar, NavSettings& v);

	// The navigation settings of a source; nullopt if the source has none (a Library, an older pack) or they are damaged.
	std::optional<NavSettings> read_nav_settings(IAssetSource& source);
}
