#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "panel.h"

#include <asset_pipeline/asset_database.h>

// The "Assets" panel: a folder tree of the source roots known to the AssetDatabase (assets/ and the engine's own assets) beside the
// files of the selected folder, with a search box that filters the files of the whole tree. Files can be dragged out (payload
// "WI_ASSET": the source path as a null-terminated string). Main thread, between the ImGui frame's begin and end.
class AssetBrowserPanel final : public Panel
{
public:
	explicit AssetBrowserPanel(wiwa::AssetDatabase& database);

	void on_update() override;
	void on_draw() override;

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

	const FolderEntry* find_folder(const std::filesystem::path& path) const;
	const std::filesystem::path* find_root(const std::filesystem::path& path) const;
	void collect_matches(const FolderEntry& folder, std::vector<const FileEntry*>& out) const;

	wiwa::AssetDatabase& m_Database;

	std::vector<FolderEntry> m_Roots{}; // one per source root of the database
	std::filesystem::path m_CurrentDir{};
	std::filesystem::path m_Selected{};

	char m_Search[128] = "";
	float m_ThumbnailScale = 1.f;       // scales the file cells
	float m_RescanTimer = 0.f;          // seconds since the tree was last read from disk
};
