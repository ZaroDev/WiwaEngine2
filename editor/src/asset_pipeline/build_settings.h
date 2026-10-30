#pragma once

#include <assets/asset_guid.h>

namespace wiwa
{
	// The file name of the build settings, in the editor's working directory.
	constexpr const char* BUILD_SETTINGS_FILE = "build_settings.json";

	// What goes into assets.wpak: the scenes a game ships (and, through them, everything they use), the one it starts with, and
	// assets to ship although no scene uses them (a Lua script loads them by path, say). Asset guids, so a rename breaks nothing.
	struct BuildSettings
	{
		AssetGuid DefaultScene{}; // all zero: none
		std::vector<AssetGuid> Scenes{};
		std::vector<AssetGuid> AlwaysInclude{};

		// nullopt with an empty error if there is no such file, nullopt with a message if it cannot be read.
		static std::optional<BuildSettings> load(const std::filesystem::path& path, std::string& error);
		bool save(const std::filesystem::path& path) const;

		// False (and why) if the default scene is set but is not one of Scenes.
		bool validate(std::string& error) const;
	};
}
