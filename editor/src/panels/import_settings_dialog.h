#pragma once

#include <deque>
#include <filesystem>
#include <optional>
#include <string>

#include <asset_pipeline/asset_database.h>
#include <asset_pipeline/import_settings_model.h>

// The "Import settings" modal: one widget per option of the asset's importer (drawn from its schema), with Apply (stores the values and cooks the
// asset again), Revert, Defaults and Cancel. Requests queue, so several dropped files are handled one after the other. Main thread.
class ImportSettingsDialog
{
public:
	explicit ImportSettingsDialog(wiwa::AssetDatabase& database) : m_Database(database) {}

	// Asks for the dialog of this file. Files that are not editable are ignored.
	void request(const std::filesystem::path& source);
	// Call every frame, from where ImGui windows can be drawn (it does nothing while there is nothing to show).
	void draw();

private:
	void next();

	wiwa::AssetDatabase& m_Database;
	std::deque<std::filesystem::path> m_Queue{};
	std::optional<wiwa::ImportSettingsModel> m_Model{};
	std::string m_Error{};
	bool m_OpenPopup = false;
};
