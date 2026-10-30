#include <editor_common.h>
#include "asset_browser_panel.h"

#include <imgui.h>

#include <asset_pipeline/build_settings.h>
#include <asset_pipeline/meta_file.h>

#include <IconFontCppHeaders/FontIcons/IconsForkAwesome.h>

#ifdef WI_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <shellapi.h>
#endif

namespace
{
	constexpr const char* ASSET_PAYLOAD = "WI_ASSET";
	constexpr float RESCAN_INTERVAL = 1.0f;

	std::string lower(std::string text)
	{
		std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return text;
	}

	// True if path is root or lies below it.
	bool is_within(const std::filesystem::path& path, const std::filesystem::path& root)
	{
		const std::filesystem::path relative = path.lexically_normal().lexically_relative(root.lexically_normal());
		return !relative.empty() && *relative.begin() != "..";
	}

	void open_externally(const std::filesystem::path& path)
	{
#ifdef WI_PLATFORM_WINDOWS
		ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
		(void)path;
#endif
	}
}

AssetBrowserPanel::AssetBrowserPanel(wiwa::AssetDatabase& database)
	: Panel("Asset Browser", PanelCategory::Window), m_Database(database)
{
	rescan();
	if (!m_Roots.empty())
	{
		m_CurrentDir = m_Roots.front().Path;
	}
}

void AssetBrowserPanel::on_update()
{
	if (!m_Open)
	{
		return;
	}

	m_RescanTimer += ImGui::GetIO().DeltaTime;
	if (m_RescanTimer >= RESCAN_INTERVAL)
	{
		rescan();
	}
}

void AssetBrowserPanel::rescan()
{
	m_RescanTimer = 0.f;

	m_Roots.clear();
	for (const wiwa::AssetDatabase::SourceRoot& root : m_Database.get_roots())
	{
		std::error_code error;
		if (!std::filesystem::is_directory(root.Source, error))
		{
			continue;
		}

		FolderEntry folder{};
		folder.Path = root.Source;
		scan_folder(folder);
		m_Roots.push_back(std::move(folder));
	}

	// the folder that was open can have been deleted or renamed
	if (!m_CurrentDir.empty() && find_folder(m_CurrentDir) == nullptr)
	{
		const std::filesystem::path* root = find_root(m_CurrentDir);
		m_CurrentDir = root != nullptr ? *root : (m_Roots.empty() ? std::filesystem::path{} : m_Roots.front().Path);
	}
}

void AssetBrowserPanel::scan_folder(FolderEntry& folder)
{
	std::error_code error;
	for (std::filesystem::directory_iterator it(folder.Path, error), end; !error && it != end; it.increment(error))
	{
		const std::filesystem::path& path = it->path();
		if (it->is_directory(error))
		{
			FolderEntry child{};
			child.Path = path;
			scan_folder(child);
			folder.Children.push_back(std::move(child));
		}
		else if (path.extension() != ".meta") // the database's sidecar files are not assets
		{
			folder.Files.push_back({ path });
		}
	}

	const auto byName = [](const auto& a, const auto& b) { return a.Path.filename() < b.Path.filename(); };
	std::sort(folder.Children.begin(), folder.Children.end(), byName);
	std::sort(folder.Files.begin(), folder.Files.end(), byName);
}

const AssetBrowserPanel::FolderEntry* AssetBrowserPanel::find_folder(const std::filesystem::path& path) const
{
	const std::filesystem::path wanted = path.lexically_normal();

	std::vector<const FolderEntry*> pending;
	for (const FolderEntry& root : m_Roots)
	{
		pending.push_back(&root);
	}
	while (!pending.empty())
	{
		const FolderEntry* folder = pending.back();
		pending.pop_back();
		if (folder->Path.lexically_normal() == wanted)
		{
			return folder;
		}
		for (const FolderEntry& child : folder->Children)
		{
			pending.push_back(&child);
		}
	}
	return nullptr;
}

const std::filesystem::path* AssetBrowserPanel::find_root(const std::filesystem::path& path) const
{
	for (const FolderEntry& root : m_Roots)
	{
		if (is_within(path, root.Path))
		{
			return &root.Path;
		}
	}
	return nullptr;
}

void AssetBrowserPanel::collect_matches(const FolderEntry& folder, std::vector<const FileEntry*>& out) const
{
	const std::string needle = lower(m_Search);
	for (const FolderEntry& child : folder.Children)
	{
		collect_matches(child, out);
	}
	for (const FileEntry& file : folder.Files)
	{
		if (lower(file.Path.filename().string()).find(needle) != std::string::npos)
		{
			out.push_back(&file);
		}
	}
}

void AssetBrowserPanel::on_draw()
{
	ImGui::SetNextWindowPos(ImVec2(12.0f, 680.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(700.0f, 280.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(get_name(), &m_Open))
	{
		if (ImGui::BeginTable("##asset_browser", 2, ImGuiTableFlags_Resizable))
		{
			ImGui::TableSetupColumn("folders", ImGuiTableColumnFlags_WidthFixed, 180.0f);
			ImGui::TableSetupColumn("files", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableNextColumn();
			draw_folder_tree();
			ImGui::TableNextColumn();
			draw_top_bar();
			ImGui::Separator();
			draw_file_grid();
			ImGui::EndTable();
		}
	}
	ImGui::End();
}

void AssetBrowserPanel::draw_folder_tree()
{
	ImGui::BeginChild("##folders");
	for (const FolderEntry& root : m_Roots)
	{
		draw_folder_node(root);
	}
	ImGui::EndChild();
}

void AssetBrowserPanel::draw_folder_node(const FolderEntry& folder)
{
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_SpanAvailWidth;
	if (folder.Children.empty())
	{
		flags |= ImGuiTreeNodeFlags_Leaf;
	}
	if (m_CurrentDir == folder.Path)
	{
		flags |= ImGuiTreeNodeFlags_Selected;
	}

	const std::string label = std::string(ICON_FK_FOLDER " ") + folder.Path.filename().string();
	const bool open = ImGui::TreeNodeEx(folder.Path.string().c_str(), flags, "%s", label.c_str());
	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
	{
		m_CurrentDir = folder.Path;
	}
	if (open)
	{
		for (const FolderEntry& child : folder.Children)
		{
			draw_folder_node(child);
		}
		ImGui::TreePop();
	}
}

void AssetBrowserPanel::draw_top_bar()
{
	const std::filesystem::path* root = find_root(m_CurrentDir);
	const bool atRoot = root == nullptr || m_CurrentDir.lexically_normal() == root->lexically_normal();

	ImGui::BeginDisabled(atRoot);
	if (ImGui::Button(ICON_FK_BACKWARD))
	{
		m_CurrentDir = m_CurrentDir.parent_path();
	}
	ImGui::EndDisabled();

	ImGui::SameLine();
	if (ImGui::Button(ICON_FK_REFRESH))
	{
		rescan();
	}

	ImGui::SameLine();
	ImGui::TextUnformatted(m_CurrentDir.generic_string().c_str());

	ImGui::SetNextItemWidth(160.0f);
	ImGui::InputTextWithHint("##asset_search", "Search", m_Search, sizeof(m_Search));
	ImGui::SameLine();
	ImGui::SetNextItemWidth(100.0f);
	ImGui::SliderFloat("##asset_zoom", &m_ThumbnailScale, 0.5f, 2.0f, "Size %.1f");
}

void AssetBrowserPanel::draw_file_grid()
{
	const float padding = 16.0f * m_ThumbnailScale;
	const float thumbnailSize = 64.0f * m_ThumbnailScale;
	const float cellSize = thumbnailSize + padding;

	std::vector<const FileEntry*> matches;
	std::vector<const FolderEntry*> folders;
	const bool searching = m_Search[0] != '\0';
	if (searching)
	{
		for (const FolderEntry& root : m_Roots)
		{
			collect_matches(root, matches);
		}
	}
	else if (const FolderEntry* current = find_folder(m_CurrentDir))
	{
		for (const FolderEntry& child : current->Children)
		{
			folders.push_back(&child);
		}
		for (const FileEntry& file : current->Files)
		{
			matches.push_back(&file);
		}
	}

	ImGui::BeginChild("##files");
	const int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cellSize));
	if (ImGui::BeginTable("##assets", columns))
	{
		ImGui::TableNextColumn();

		// folders first, as the file explorer does; a double click opens one
		for (const FolderEntry* folder : folders)
		{
			ImGui::PushID(folder->Path.string().c_str());
			if (ImGui::Button(ICON_FK_FOLDER, ImVec2(thumbnailSize, thumbnailSize)))
			{
				m_Selected = folder->Path;
			}
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				m_CurrentDir = folder->Path;
			}
			ImGui::TextWrapped("%s", folder->Path.filename().string().c_str());
			ImGui::PopID();
			ImGui::TableNextColumn();
		}

		for (const FileEntry* file : matches)
		{
			ImGui::PushID(file->Path.string().c_str());
			draw_file(file->Path, thumbnailSize);
			ImGui::PopID();
			ImGui::TableNextColumn();
		}

		ImGui::EndTable();
	}

	draw_context_menu();
	ImGui::EndChild();
}

void AssetBrowserPanel::draw_file(const std::filesystem::path& file, float thumbnailSize)
{
	const bool selected = m_Selected == file;
	if (selected)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
	}
	if (ImGui::Button(ICON_FK_FILE_O, ImVec2(thumbnailSize, thumbnailSize)))
	{
		m_Selected = file;
	}
	if (selected)
	{
		ImGui::PopStyleColor();
	}

	if (ImGui::BeginDragDropSource())
	{
		const std::string path = file.generic_string();
		ImGui::SetDragDropPayload(ASSET_PAYLOAD, path.c_str(), path.size() + 1);
		ImGui::TextUnformatted(file.filename().string().c_str());
		ImGui::EndDragDropSource();
	}
	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
	{
		open_externally(file);
	}
	ImGui::TextWrapped("%s", file.filename().string().c_str());
}

void AssetBrowserPanel::draw_context_menu()
{
	if (!ImGui::BeginPopupContextWindow("##assets_context", ImGuiPopupFlags_MouseButtonRight))
	{
		return;
	}

	if (ImGui::MenuItem(ICON_FK_FOLDER " New folder"))
	{
		std::error_code error;
		std::filesystem::create_directory(m_CurrentDir / "New folder", error);
		rescan();
	}

	std::error_code error;
	if (!m_Selected.empty() && std::filesystem::exists(m_Selected, error))
	{
		ImGui::Separator();
		ImGui::TextDisabled("%s", m_Selected.filename().string().c_str());

		// pack it although no packed scene uses it (the Build Settings panel lists these)
		if (std::optional<wiwa::MetaFile> meta = wiwa::MetaFile::load(wiwa::MetaFile::meta_path_for(m_Selected)))
		{
			std::string loadError;
			wiwa::BuildSettings settings = wiwa::BuildSettings::load(wiwa::BUILD_SETTINGS_FILE, loadError).value_or(wiwa::BuildSettings{});
			auto listed = std::find(settings.AlwaysInclude.begin(), settings.AlwaysInclude.end(), meta->Guid);

			// a file that cannot be read is left alone: saving would overwrite it
			if (ImGui::MenuItem("Always include in build", nullptr, listed != settings.AlwaysInclude.end(), loadError.empty()))
			{
				if (listed != settings.AlwaysInclude.end())
				{
					settings.AlwaysInclude.erase(listed);
				}
				else
				{
					settings.AlwaysInclude.push_back(meta->Guid);
				}
				settings.save(wiwa::BUILD_SETTINGS_FILE);
			}
		}

		if (ImGui::MenuItem(ICON_FK_TRASH " Delete"))
		{
			// the .meta goes with its file; the database cleans up the cooked files at its next scan
			std::filesystem::remove_all(m_Selected, error);
			std::filesystem::remove(std::filesystem::path(m_Selected) += ".meta", error);
			m_Selected.clear();
			rescan();
		}
	}

	ImGui::Separator();
	if (ImGui::MenuItem("Open in file explorer"))
	{
		open_externally(m_CurrentDir);
	}

	ImGui::EndPopup();
}
