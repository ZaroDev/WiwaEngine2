#pragma once

#include <filesystem>
#include <string>

#include <nav/nav_project_settings.h>

// The project navigation settings (nav.wsettings, in the editor's working directory next to audio.wsettings): JSON with "areas", a list of
// { "id": 2..15, "name": "Mud", "cost": 8 } (id 1, Walkable, may be listed for its cost; a cost is a number from 1; an id that is not listed is unused).
// The first format, "areaCost" (an object of the names Walkable, Mud, Water, Custom0..Custom3 and their costs), is still read. The packager puts them
// into assets.wpak (nav/nav_project_settings.h), the Navigation panel edits them.
namespace wiwa
{
	constexpr const char* NAV_SETTINGS_FILE = "nav.wsettings";

	// The fixed name of an area id as scene files store it (the serialized value of a NavAreaTag, which must not change when an area is renamed):
	// NotWalkable, Walkable, Mud, Water, Custom0..Custom3, Area8..Area15.
	const char* nav_area_name(NavArea area);

	// Missing keys keep their defaults, unknown keys are ignored, costs are sanitized. False, and why in error, for text that is not a JSON object,
	// an "areas" that is not a list, an id out of range or listed twice, a name that is not valid or that another area has, a cost that is not a number,
	// or the legacy "areaCost" naming an unknown area.
	bool nav_settings_from_text(const std::string& text, NavSettings& out, std::string& error);
	std::string nav_settings_to_text(const NavSettings& settings);

	// A file that does not exist gives the default settings and true; a file that cannot be read or parsed gives false and the default.
	bool load_nav_settings(const std::filesystem::path& path, NavSettings& out, std::string& error);
	bool save_nav_settings(const std::filesystem::path& path, const NavSettings& settings, std::string& error);
}
