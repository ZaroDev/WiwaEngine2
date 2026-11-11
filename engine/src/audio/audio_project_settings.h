#pragma once

#include <optional>
#include <string>
#include <vector>

#include <assets/asset_guid.h>
#include <assets/asset_source.h>
#include <assets/wasset_io.h>

#include "audio_types.h"

namespace wiwa
{
	// The key a pack stores the project audio settings under (it is not an asset, so no real guid is this one). The editor writes the settings to
	// audio.wsettings and the packager puts them in assets.wpak; a game reads them from there.
	constexpr AssetGuid AUDIO_SETTINGS_GUID{ ~0ull, ~0ull - 5 };

	constexpr u32 AUDIO_SETTINGS_VERSION = 1;
	constexpr u32 AUDIO_POOL_MIN_KB = 1024;
	constexpr u32 AUDIO_POOL_MAX_KB = 262144;

	struct AudioProjectSettings
	{
		AudioBackendKind Backend = AudioBackendKind::Null;
		std::vector<AssetRef> Banks{};         // SoundBank assets, loaded in this order (Init first, whatever the order)
		u32 MemoryPoolKb = 16384;
		std::string Language = "English(US)"; // the vendor language the voice banks are picked for

		bool operator==(const AudioProjectSettings&) const = default;
	};

	// The pool is kept inside AUDIO_POOL_MIN_KB..AUDIO_POOL_MAX_KB, an empty language gets the default, invalid and repeated banks are dropped.
	AudioProjectSettings sanitized(AudioProjectSettings settings);

	// The backend of its name ("Null", "Wwise"); nullopt for any other.
	std::optional<AudioBackendKind> audio_backend_from_name(const std::string& name);

	// A version, then the values. A blob that is not of this version, names an unknown backend or does not read to the end is refused.
	void archive(BinaryArchive& ar, AudioProjectSettings& v);

	// The audio settings of a source; nullopt if the source has none (a Library, an older pack) or they are damaged.
	std::optional<AudioProjectSettings> read_audio_settings(IAssetSource& source);
}
