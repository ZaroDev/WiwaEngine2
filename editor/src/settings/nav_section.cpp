#include <editor_common.h>
#include "nav_section.h"

#include <core/engine.h>
#include <nav/nav_module.h>

#include <asset_pipeline/nav_settings_file.h>

NavSection::NavSection(std::filesystem::path file, bool applyToEngine)
	: m_File(std::move(file)), m_ApplyToEngine(applyToEngine)
{
	std::string error;
	if (!wiwa::load_nav_settings(m_File, m_Settings, error))
	{
		WI_WARN("Nav", "{0}: {1}", m_File.string(), error);
	}
	apply();
}

bool NavSection::load()
{
	wiwa::NavSettings loaded;
	std::string error;
	if (!wiwa::load_nav_settings(m_File, loaded, error))
	{
		m_Error = error;
		return false;
	}
	m_Error.clear();
	m_Settings = loaded;
	apply();
	return true;
}

bool NavSection::save()
{
	m_Error.clear();
	return wiwa::save_nav_settings(m_File, m_Settings, m_Error);
}

void NavSection::apply()
{
	if (!m_ApplyToEngine)
	{
		return;
	}
	if (wiwa::NavModule* module = wiwa::Engine::get().get_module<wiwa::NavModule>())
	{
		module->set_settings(m_Settings);
	}
}

bool NavSection::set_cost(wiwa::NavArea area, float cost)
{
	wiwa::NavSettings next = m_Settings;
	next.AreaCost[static_cast<size_t>(area)] = cost;
	next = wiwa::sanitized(next);
	if (next == m_Settings)
	{
		return true;
	}

	const wiwa::NavSettings previous = m_Settings;
	m_Settings = next;
	if (!save())
	{
		m_Settings = previous;
		return false;
	}
	apply();
	return true;
}
