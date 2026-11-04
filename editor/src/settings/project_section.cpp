#include <editor_common.h>
#include "project_section.h"

#include <core/engine.h>
#include <renderer/window.h>

ProjectSection::ProjectSection(std::filesystem::path file)
	: m_File(std::move(file)), m_Settings(wiwa::Engine::get().project())
{
}

bool ProjectSection::load()
{
	m_Error.clear();
	std::string error;
	if (const std::optional<wiwa::ProjectSettings> loaded = wiwa::load_project_settings(m_File, error))
	{
		m_Settings = *loaded;
		return true;
	}
	if (!error.empty())
	{
		m_Error = error;
		return false;
	}
	return true; // no file yet: the values stay as they are
}

bool ProjectSection::save()
{
	m_Error.clear();
	if (!wiwa::save_project_settings(m_File, m_Settings))
	{
		m_Error = "cannot write " + m_File.string();
		return false;
	}
	return true;
}

void ProjectSection::apply()
{
	wiwa::Engine& engine = wiwa::Engine::get();
	engine.set_project(m_Settings);
	if (wiwa::Window* window = engine.get_module<wiwa::Window>())
	{
		window->set_title(editor_title(m_Settings));
	}
}

bool ProjectSection::set(wiwa::ProjectSettings settings)
{
	m_Settings = wiwa::sanitized(std::move(settings));
	apply();
	return save();
}

std::string ProjectSection::editor_title(const wiwa::ProjectSettings& settings)
{
	return fmt::format("Wiwa2 Editor - ({0} {1}) <{2}>", settings.Name, wiwa::version_string(settings), WI_BUILD_CONFIGURATION);
}
