#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <scene/reflection/field_descriptor.h>

#include "../asset_editor/asset_editor.h"
#include "bt_canvas.h"
#include "bt_edit_model.h"

namespace wiwa
{
	// The tools for a behaviour tree (.wbt) in the Asset Editor panel: the node canvas in the big column; in the tool pane the selected node's name and
	// parameters (blackboard keys offered by type, Lua leaves with their properties), the blackboard, and the problems validate_bt finds (a click selects
	// the node). Save writes the file when it has no error. All edits go through BtEditModel (undo, redo).
	class BehaviorTreeEditor final : public IAssetEditor
	{
	public:
		explicit BehaviorTreeEditor(AssetEditorHost host) : m_Host(host) {}

		bool load(const std::filesystem::path& file, std::string& error) override;
		const char* kind() const override { return "Behaviour tree"; }
		const std::filesystem::path& file() const override { return m_File; }
		AssetRef preview_skeleton() const override { return {}; }

		bool draws_own_preview() const override { return true; }
		void draw_preview(ImVec2 size) override;
		void draw_tools() override;

		bool dirty() const override { return m_Model && m_Model->dirty(); }
		bool save(std::string& error) override;
		void undo() override { if (m_Model) { m_Model->undo(); } }
		void redo() override { if (m_Model) { m_Model->redo(); } }
		bool can_undo() const override { return m_Model && m_Model->can_undo(); }
		bool can_redo() const override { return m_Model && m_Model->can_redo(); }

		// For checks.
		BtEditModel* model() { return m_Model.get(); }
		const std::string& selected() const { return m_Selected; }
		void select(const std::string& id) { m_Selected = id; }
		const std::vector<BtLeafScript>& leaf_scripts() const { return m_Scripts; }
		u32 frames_drawn() const { return m_Frames; }

	private:
		struct LeafInfo
		{
			BtLeafScript Script{};
			std::vector<FieldDescriptor> Properties{};
		};

		void refresh_scripts();
		void draw_node();
		void draw_params(const BtNodeDesc& node);
		void draw_script_properties(const BtNodeDesc& node);
		void draw_blackboard();
		void draw_problems();
		bool text_field(const char* id, const std::string& current, std::string& result, float width = 180.0f);
		// Wraps a held widget in one undo step.
		void gesture_of_last_item();
		bool number_field(const std::string& node, const char* param, double current, float minimum, float maximum, float speed = 0.05f);
		std::vector<std::string> keys_of(bool vectorOrEntityOnly) const;

		AssetEditorHost m_Host;
		std::filesystem::path m_File{};
		std::unique_ptr<BtEditModel> m_Model{};
		BtCanvas m_Canvas{};
		std::string m_Selected{};
		std::vector<LeafInfo> m_Leaves{};
		std::vector<BtLeafScript> m_Scripts{};
		bool m_ScriptsLoaded = false;
		bool m_ToolGesture = false;
		bool m_FitNext = true;
		u32 m_Frames = 0;
		std::string m_Message{};
		std::string m_EditKey{};
		char m_EditBuffer[256] = "";
		char m_NewKey[64] = "";
		int m_NewKeyType = 0;
		std::string m_ForceKey{};     // a remove or retype that was refused because nodes use the key: offered again with "anyway"
		int m_ForceType = -1;         // the new type for a retype, -1 for a remove
	};
}
