#pragma once

#include <filesystem>

#include <imgui.h>
#include <string>

#include <assets/asset_guid.h>
#include <assets/asset_data.h>

namespace wiwa
{
	class AssetDatabase;
	class AssetPreview;
	class EditorState;
	class SkeletonIndex;

	// What the editors in the Asset Editor panel share.
	struct AssetEditorHost
	{
		AssetDatabase& Database;
		EditorState& State;
		SkeletonIndex& Index;
		AssetPreview& Preview;
	};

	// One asset type's tools in the Asset Editor panel: it loads the native file, draws its tool pane, and saves. The panel owns the preview, the
	// title, the dirty prompts and the keys; the editor owns the document (an edit model) and what the preview shows for it.
	class IAssetEditor
	{
	public:
		virtual ~IAssetEditor() = default;

		// False, and why in error, if the file cannot be read.
		virtual bool load(const std::filesystem::path& file, std::string& error) = 0;
		virtual const char* kind() const = 0; // "Skeleton", "Skinned mesh"
		virtual const std::filesystem::path& file() const = 0;

		// The skeleton the preview poses, and the mesh the editor itself shows (invalid: the panel's choice of mesh for that skeleton).
		virtual AssetRef preview_skeleton() const = 0;
		virtual AssetRef own_mesh() const { return {}; }
		// The clip the preview plays: the one a clip editor edits (invalid: the panel's clip choice).
		virtual AssetRef own_clip() const { return {}; }
		// The graph the preview plays: the one a graph editor edits (invalid: none).
		virtual AssetRef own_graph() const { return {}; }
		// The skeleton being edited, for the overlays (names, sockets); null when the editor edits no skeleton.
		virtual const SkeletonData* edit_skeleton() const { return nullptr; }
		virtual int selected_joint() const { return -1; }
		virtual int selected_socket() const { return -1; }

		virtual void draw_tools() = 0;
		// An editor with a wide part (a timeline) gets the full width under the preview and the tools.
		virtual bool has_bottom_pane() const { return false; }
		virtual void draw_bottom(ImVec2 size) { (void)size; }
		// Over the preview image, after it is drawn (a gizmo).
		virtual void draw_gizmo() {}

		virtual bool dirty() const = 0;
		// Writes the file. False with an empty error means the editor asked the user something first (a popup in draw_tools) and will save when
		// confirmed; false with an error is a failure.
		virtual bool save(std::string& error) = 0;
		virtual void undo() = 0;
		virtual void redo() = 0;
		virtual bool can_undo() const = 0;
		virtual bool can_redo() const = 0;
	};
}
