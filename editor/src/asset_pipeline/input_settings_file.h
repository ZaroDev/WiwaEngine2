#pragma once

#include <filesystem>
#include <string>

#include <input/input_project_settings.h>

// The project's input settings (input.wsettings, in the editor's working directory next to physics.wsettings): JSON with "defaultMap", the
// guid (32 hex characters) of the .winput asset with the default action maps, or an empty string. The packager puts them into assets.wpak
// (input/input_project_settings.h), the Input panel edits them.
namespace wiwa
{
	constexpr const char* INPUT_SETTINGS_FILE = "input.wsettings";

	bool input_settings_from_text(const std::string& text, InputProjectSettings& out, std::string& error);
	std::string input_settings_to_text(const InputProjectSettings& settings);

	// A file that does not exist gives the default settings and true; a file that cannot be read or parsed gives false and the default.
	bool load_input_settings(const std::filesystem::path& path, InputProjectSettings& out, std::string& error);
	bool save_input_settings(const std::filesystem::path& path, const InputProjectSettings& settings, std::string& error);
}
