#pragma once

#include <filesystem>
#include <string>

#include <core/project_settings.h>

#include <string>

// The project's identity, window, launch and performance values (project.wproject). set() sanitizes, saves, and applies them: the
// engine's copy is replaced and the editor's title bar shows the new name and version. A game reads the file from its pack.
class ProjectSection final
{
public:
	// `file`: where the settings are saved (project.wproject in the editor's working directory). Starts from what the engine runs with.
	explicit ProjectSection(std::filesystem::path file = wiwa::PROJECT_SETTINGS_FILE);

	const char* name() const { return "Project"; }
	bool load();
	bool save();
	void apply();
	const std::string& last_error() const { return m_Error; }

	const wiwa::ProjectSettings& settings() const { return m_Settings; }
	// Sanitizes and keeps the value, saves it and applies it. False, and why in last_error, if the save failed (it is still applied).
	bool set(wiwa::ProjectSettings settings);

	// The editor's window title for a project: "Wiwa2 Editor - (Name 1.2.3) <Debug>".
	static std::string editor_title(const wiwa::ProjectSettings& settings);

private:
	std::filesystem::path m_File;
	wiwa::ProjectSettings m_Settings{};
	std::string m_Error{};
};
