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

bool NavSection::commit(wiwa::NavSettings next)
{
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

bool NavSection::set_cost(wiwa::u32 id, float cost)
{
	m_Error.clear();
	if (!m_Settings.used(id))
	{
		m_Error = "that area does not exist";
		return false;
	}
	wiwa::NavSettings next = m_Settings;
	next.AreaCost[id] = cost;
	return commit(next);
}

bool NavSection::add_area(const std::string& name, float cost, wiwa::u32& id)
{
	m_Error.clear();
	wiwa::u32 slot = 2;
	while (slot < wiwa::NAV_AREA_COUNT && m_Settings.used(slot))
	{
		slot++;
	}
	if (slot >= wiwa::NAV_AREA_COUNT)
	{
		m_Error = "there are already " + std::to_string(wiwa::NAV_AREA_COUNT) + " areas: remove one first";
		return false;
	}
	if (!wiwa::nav_area_name_ok(m_Settings, slot, name))
	{
		m_Error = "an area name must not be empty, longer than " + std::to_string(wiwa::NAV_AREA_NAME_MAX) + " characters, hold a control character or be one another area has";
		return false;
	}
	wiwa::NavSettings next = m_Settings;
	next.AreaName[slot] = name;
	next.AreaCost[slot] = cost;
	if (!commit(next))
	{
		return false;
	}
	id = slot;
	return true;
}

bool NavSection::rename_area(wiwa::u32 id, const std::string& name)
{
	m_Error.clear();
	if (id < 2 || !m_Settings.used(id))
	{
		m_Error = "that area cannot be renamed";
		return false;
	}
	if (name == m_Settings.AreaName[id])
	{
		return true;
	}
	if (!wiwa::nav_area_name_ok(m_Settings, id, name))
	{
		m_Error = "an area name must not be empty, longer than " + std::to_string(wiwa::NAV_AREA_NAME_MAX) + " characters, hold a control character or be one another area has";
		return false;
	}
	wiwa::NavSettings next = m_Settings;
	next.AreaName[id] = name;
	return commit(next);
}

bool NavSection::remove_area(wiwa::u32 id)
{
	m_Error.clear();
	if (id < 2 || !m_Settings.used(id))
	{
		m_Error = "that area cannot be removed";
		return false;
	}
	wiwa::NavSettings next = m_Settings;
	next.AreaName[id].clear();
	next.AreaCost[id] = 1.0f;
	return commit(next);
}
