#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "panel.h"

#include <asset_pipeline/asset_database.h>
#include <asset_pipeline/build_settings.h>

// The "Build Settings" panel: which scenes the pack (assets.wpak) holds, which one the game starts with, and assets to pack although no
// scene uses them. Every change is saved to build_settings.json at once; the file is read again every second, so the asset browser can
// edit it too. Main thread, between the ImGui frame's begin and end.
class BuildSettingsPanel final : public Panel
{
public:
	explicit BuildSettingsPanel(wiwa::AssetDatabase& database)
		: Panel("Build Settings", PanelCategory::Window), m_Database(database) {}

	void on_draw() override;

private:
	struct SceneRow
	{
		std::string Name{}; // relative to the assets folder
		wiwa::AssetGuid Guid{};
	};

	void refresh();
	void save();

	wiwa::AssetDatabase& m_Database;
	wiwa::BuildSettings m_Settings{};
	std::vector<SceneRow> m_Scenes{};
	std::string m_LoadError{};
	double m_RefreshedAt = -10.0;
};
