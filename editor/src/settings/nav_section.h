#pragma once

#include <filesystem>
#include <string>

#include <nav/nav_project_settings.h>

#include <string>

// The project navigation settings (nav.wsettings): the areas (a name and the cost of walking one metre of each; up to 16, ids 0 and 1 fixed). Each setter saves at once and applies to the navigation
// module (a world already running keeps its costs until the next play); a failed save leaves the value as it was. Shown in the Navigation panel.
class NavSection final
{
public:
	// `file`: where the settings are saved. `applyToEngine` false: they are only saved (a check must not touch the running module).
	explicit NavSection(std::filesystem::path file, bool applyToEngine = true);

	const char* name() const { return "Navigation"; }
	bool load();
	bool save();
	void apply();
	const std::string& last_error() const { return m_Error; }

	const wiwa::NavSettings& settings() const { return m_Settings; }

	// The cost of one area, from 1 up (anything else is brought into that range). False (and last_error) for an unused slot or a failed save.
	bool set_cost(wiwa::u32 id, float cost);
	bool set_cost(wiwa::NavArea area, float cost) { return set_cost(static_cast<wiwa::u32>(area), cost); }

	// A new area in the lowest free slot (2..15) with that name and cost; its id in `id`. False, and why in last_error, for a name that is not valid or
	// that an area has, no free slot, or a failed save.
	bool add_area(const std::string& name, float cost, wiwa::u32& id);

	// Renames an area of the slot range; the id (what baked meshes and tags store) stays. Same refusals.
	bool rename_area(wiwa::u32 id, const std::string& name);

	// Frees a slot of the slot range: its name and cost go. Entities tagged with it bake as Walkable (with a warning) and polygons already baked keep the
	// id at cost 1 until the next bake.
	bool remove_area(wiwa::u32 id);

private:
	// Applies `next` (sanitized) and saves it; a failed save puts the old settings back.
	bool commit(wiwa::NavSettings next);

	std::filesystem::path m_File;
	wiwa::NavSettings m_Settings{};
	std::string m_Error{};
	bool m_ApplyToEngine = true;
};
