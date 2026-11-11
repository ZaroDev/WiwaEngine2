#pragma once

#include <filesystem>
#include <string>

#include <nav/nav_project_settings.h>

#include "settings_section.h"

// The project navigation settings (nav.wsettings): the cost of walking each kind of ground. Each setter saves at once and applies to the navigation
// module (a world already running keeps its costs until the next play); a failed save leaves the value as it was. Shown in the Navigation panel.
class NavSection final : public ISettingsSection
{
public:
	// `file`: where the settings are saved. `applyToEngine` false: they are only saved (a check must not touch the running module).
	explicit NavSection(std::filesystem::path file, bool applyToEngine = true);

	virtual const char* name() const override { return "Navigation"; }
	virtual bool load() override;
	virtual bool save() override;
	virtual void apply() override;
	virtual const std::string& last_error() const override { return m_Error; }

	const wiwa::NavSettings& settings() const { return m_Settings; }

	// The cost of one area, from 1 up (anything else is brought into that range).
	bool set_cost(wiwa::NavArea area, float cost);

private:
	std::filesystem::path m_File;
	wiwa::NavSettings m_Settings{};
	std::string m_Error{};
	bool m_ApplyToEngine = true;
};
