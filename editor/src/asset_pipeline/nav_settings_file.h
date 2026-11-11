#pragma once

#include <filesystem>
#include <string>

#include <nav/nav_project_settings.h>

// The project navigation settings (nav.wsettings, in the editor's working directory next to audio.wsettings): JSON with "areaCost", an object that
// names an area ("Walkable", "Mud", "Water", "Custom0".."Custom3") and gives its cost (a number from 1). The packager puts them into assets.wpak
// (nav/nav_project_settings.h), the Settings page edits them.
namespace wiwa
{
	constexpr const char* NAV_SETTINGS_FILE = "nav.wsettings";

	// The name of an area as the file and the editor show it.
	const char* nav_area_name(NavArea area);

	// Missing keys keep their defaults, unknown keys are ignored, costs are sanitized. False, and why in error, for text that is not a JSON object,
	// an "areaCost" that is not an object, an unknown area name or a cost that is not a number.
	bool nav_settings_from_text(const std::string& text, NavSettings& out, std::string& error);
	std::string nav_settings_to_text(const NavSettings& settings);

	// A file that does not exist gives the default settings and true; a file that cannot be read or parsed gives false and the default.
	bool load_nav_settings(const std::filesystem::path& path, NavSettings& out, std::string& error);
	bool save_nav_settings(const std::filesystem::path& path, const NavSettings& settings, std::string& error);
}
