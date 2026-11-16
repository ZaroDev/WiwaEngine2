#pragma once

#include <filesystem>
#include <string>

#include <assets/asset_guid.h>
#include <input/input_project_settings.h>

#include <string>

// The project's default input map (input.wsettings). The maps themselves are edited in the Input panel.
class InputSection final
{
public:
	// `file`: where the default map is saved (input.wsettings in the editor's working directory).
	explicit InputSection(std::filesystem::path file);

	const char* name() const { return "Input"; }
	bool load();
	bool save();
	void apply();
	const std::string& last_error() const { return m_Error; }

	const wiwa::InputProjectSettings& settings() const { return m_Settings; }
	// Makes the asset the project's default input map: saves the settings file and tells the input module. False, and why in last_error, if the save failed.
	bool set_default_map(const wiwa::AssetRef& ref);

private:
	std::filesystem::path m_File;
	wiwa::InputProjectSettings m_Settings{};
	std::string m_Error{};
};
