#include <editor_common.h>
#include "input_section.h"

#include <core/engine.h>
#include <input/input_module.h>

#include <asset_pipeline/input_settings_file.h>

InputSection::InputSection(std::filesystem::path file)
	: m_File(std::move(file))
{
	std::string error;
	if (!wiwa::load_input_settings(m_File, m_Settings, error))
	{
		WI_WARN("Input", "{0}: {1}", m_File.string(), error);
	}
}

bool InputSection::load()
{
	wiwa::InputProjectSettings loaded;
	std::string error;
	if (!wiwa::load_input_settings(m_File, loaded, error))
	{
		m_Error = error;
		return false;
	}
	m_Error.clear();
	m_Settings = loaded;
	apply();
	return true;
}

bool InputSection::save()
{
	m_Error.clear();
	return wiwa::save_input_settings(m_File, m_Settings, m_Error);
}

void InputSection::apply()
{
	if (wiwa::InputModule* module = wiwa::Engine::get().get_module<wiwa::InputModule>())
	{
		module->set_default_map(m_Settings.DefaultMap);
	}
}

bool InputSection::set_default_map(const wiwa::AssetRef& ref)
{
	// the old value is kept when the save fails, as the Input panel always did
	const wiwa::InputProjectSettings previous = m_Settings;
	m_Settings.DefaultMap = ref;
	if (!save())
	{
		m_Settings = previous;
		return false;
	}
	apply();
	return true;
}
