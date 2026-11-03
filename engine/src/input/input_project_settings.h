#pragma once

#include <optional>

#include <assets/asset_guid.h>
#include <assets/asset_source.h>
#include <assets/wasset_io.h>

namespace wiwa
{
	// The key a pack stores the project's input settings under (it is not an asset, so no real guid is this one). The editor writes the
	// settings to input.wsettings and the packager puts them in assets.wpak; a game reads them from there.
	constexpr AssetGuid INPUT_SETTINGS_GUID{ ~0ull, ~0ull - 3 };

	constexpr u32 INPUT_SETTINGS_VERSION = 1;

	struct InputProjectSettings
	{
		// The .winput asset with the default action maps; invalid if the project has none.
		AssetRef DefaultMap{};
	};

	// A version, then the default map. A blob that is not of this version or does not read to the end is refused.
	void archive(BinaryArchive& ar, InputProjectSettings& v);

	// The project's input settings of a source; nullopt if the source has none (a Library, an older pack) or they are damaged.
	std::optional<InputProjectSettings> read_input_settings(IAssetSource& source);
}
