#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <panels/panel.h>

#include "asset_editor.h"
#include "asset_preview.h"
#include "editor_state.h"
#include "skeleton_index.h"

namespace wiwa
{
	class AssetDatabase;
}

// The "Asset Editor" panel: one window for the animation assets. A preview of the asset (the skeleton posed, with the skinned mesh that goes with
// it) on the left, the tools of the open asset's type on the right (IAssetEditor: SkeletonEditor, MeshEditor; the clip editor comes later). Opened
// from the Asset Browser, the Inspector or the thumbnails. Main thread, between the ImGui frame's begin and end.
class AssetEditorPanel final : public Panel
{
public:
	AssetEditorPanel(wiwa::AssetDatabase& database, wiwa::EditorState& state, wiwa::SkeletonIndex& index);
	~AssetEditorPanel() override;

	void on_update() override;
	void on_draw() override;
	const char* window_name() const override { return "###AssetEditor"; }

	// Shows the panel with this file open (.wskel or .wmesh). If the open asset has unsaved changes the user is asked first. Other kinds of file
	// are ignored (false).
	bool open_file(const std::filesystem::path& file);
	// Opens the source file of the asset, if it is one this panel edits.
	bool open_asset(const wiwa::AssetRef& ref);
	static bool can_open(const std::filesystem::path& file);

	// Lets go of the open asset and the preview's view (before the engine's modules shut down).
	void close_preview() { m_Editor.reset(); m_Preview.release(); }

	bool dirty() const { return m_Editor != nullptr && m_Editor->dirty(); }
	// Ctrl+Z, Ctrl+Y and Ctrl+S go to this panel while it is focused.
	// For the panel smoke test: the frames an editor was drawn in, the kind of editor open (empty: none) and the last error.
	unsigned frames_with_editor() const { return m_FramesWithEditor; }
	std::string open_kind() const { return m_Editor != nullptr ? m_Editor->kind() : std::string{}; }
	const std::string& last_error() const { return m_Error; }
	// The state the base layer of a graph plays in the preview (empty when no graph plays there).
	std::string preview_state() const { const wiwa::AnimatorInstance* instance = m_Preview.animator(); return instance != nullptr && instance->graph_mode() ? instance->state_name(0) : std::string{}; }
	bool focused() const { return m_Open && m_Focused; } // a closed panel is not drawn, so its last value would be stale

private:
	enum class Pending { None, Open, Close };

	bool load_now(const std::filesystem::path& file);
	void draw_toolbar();
	void draw_preview_column();
	void draw_preview_controls();
	void draw_prompt();
	void handle_keys();
	void save();
	void update_preview_choice();
	void poll_file();
	std::string title() const;

	wiwa::AssetDatabase& m_Database;
	wiwa::EditorState& m_State;
	wiwa::SkeletonIndex& m_Index;
	wiwa::AssetPreview m_Preview{};
	std::unique_ptr<wiwa::IAssetEditor> m_Editor{};

	std::string m_Error{};          // why the file could not be opened or saved
	std::string m_Status{};
	bool m_Focused = false;
	unsigned m_FramesWithEditor = 0;
	bool m_FocusRequested = false;

	Pending m_Pending = Pending::None;
	std::filesystem::path m_PendingFile{};
	bool m_PromptRequested = false;

	std::filesystem::file_time_type m_FileTime{};
	bool m_HaveFileTime = false;
	bool m_ExternalChange = false;
	int m_PollCounter = 0;

	// The preview's mesh and the clips it can play, looked up again when the index scanned or the skeleton changed.
	wiwa::AssetRef m_Skeleton{};
	wiwa::AssetRef m_PreviewMesh{};
	std::vector<wiwa::AssetRef> m_Meshes{};
	std::vector<wiwa::AssetRef> m_Clips{};
	wiwa::AssetRef m_Clip{};
	wiwa::u32 m_IndexGeneration = 0;
	bool m_ChoiceStale = true;
};
