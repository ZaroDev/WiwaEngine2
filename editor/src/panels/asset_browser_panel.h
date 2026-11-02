#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "panel.h"

#include <asset_pipeline/asset_database.h>
#include <asset_pipeline/asset_templates.h>

// The "Assets" panel: a folder tree of the source roots known to the AssetDatabase (assets/ and the engine's own assets) beside the
// files of the selected folder, with a search box that filters the files of the whole tree. Files can be dragged out (payload
// "WI_ASSET": the source path as a null-terminated string). Main thread, between the ImGui frame's begin and end.
class AssetBrowserPanel final : public Panel
{
public:
	explicit AssetBrowserPanel(wiwa::AssetDatabase& database);

	void on_update() override;
	void on_draw() override;

	// Shows the panel, goes to the folder of the asset's source file and selects the file. Does nothing for an asset the database
	// does not know.
	void select_asset(const wiwa::AssetRef& ref);

	// What opening a .wmat (a double click, or "New material") calls; the editor points it at the Material panel.
	void set_open_material(std::function<void(const std::filesystem::path&)> open) { m_OpenMaterial = std::move(open); }

	// What an entity dragged here from the Hierarchy calls (payload "WI_ENTITY"): makes a prefab of it in the folder it was dropped on, and
	// returns why that cannot be done, or an empty string. The editor points it at SceneEditor::make_prefab.
	void set_make_prefab(std::function<std::string(entt::entity, const std::filesystem::path&)> make) { m_MakePrefab = std::move(make); }

private:
	struct FileEntry
	{
		std::filesystem::path Path;
	};

	struct FolderEntry
	{
		std::filesystem::path Path;
		std::vector<FolderEntry> Children{};
		std::vector<FileEntry> Files{};
	};

	void rescan();
	static void scan_folder(FolderEntry& folder);

	void draw_top_bar();
	void draw_folder_tree();
	void draw_folder_node(const FolderEntry& folder);
	void draw_file_grid();
	void draw_file(const std::filesystem::path& file, float thumbnailSize);
	void draw_context_menu();
	// The popup that asks for the name of a new asset, and makes it.
	void draw_create_popup();
	void begin_create(wiwa::CreateKind kind);
	// Inside a BeginDragDropTarget: takes a dropped hierarchy entity and makes a prefab of it in folder.
	void accept_entity_drop(const std::filesystem::path& folder);

	const FolderEntry* find_folder(const std::filesystem::path& path) const;
	const FileEntry* find_file(const std::filesystem::path& path) const;
	const std::filesystem::path* find_root(const std::filesystem::path& path) const;
	void collect_matches(const FolderEntry& folder, std::vector<const FileEntry*>& out) const;

	wiwa::AssetDatabase& m_Database;

	std::vector<FolderEntry> m_Roots{}; // one per source root of the database
	std::filesystem::path m_CurrentDir{};
	std::filesystem::path m_Selected{};
	std::function<void(const std::filesystem::path&)> m_OpenMaterial{};
	std::function<std::string(entt::entity, const std::filesystem::path&)> m_MakePrefab{};
	std::string m_Status{};             // why the last dropped entity did not become a prefab

	wiwa::CreateKind m_CreateKind = wiwa::CreateKind::Folder;
	std::filesystem::path m_CreateDir{}; // the folder the new asset goes in: the one that was open when Create was chosen
	char m_CreateName[128] = "";
	bool m_CreateRequested = false;      // open the popup (the context menu is another popup, so it is opened from on_draw)
	bool m_CreateFocus = false;

	char m_Search[128] = "";
	float m_ThumbnailScale = 1.f;       // scales the file cells
	float m_RescanTimer = 0.f;          // seconds since the tree was last read from disk
};
