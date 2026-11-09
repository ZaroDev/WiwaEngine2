#pragma once

#include <map>
#include <memory>

#include "asset_editor.h"
#include <assets/handle.h>

#include "graph_canvas.h"
#include "graph_edit_model.h"

namespace wiwa
{
	// The tools for an animation graph (.wgraph): parameters, layers, the states of the selected layer with their clips or blend spaces, and the
	// transitions with their conditions. The preview plays the saved graph on the skeleton it names; what is edited shows there after Save.
	class GraphEditor final : public IAssetEditor
	{
	public:
		explicit GraphEditor(AssetEditorHost host) : m_Host(host) {}

		bool load(const std::filesystem::path& file, std::string& error) override;
		const char* kind() const override { return "Animation graph"; }
		const std::filesystem::path& file() const override { return m_File; }
		AssetRef preview_skeleton() const override;
		AssetRef own_graph() const override { return m_Ref; }

		void draw_tools() override;
		bool has_bottom_pane() const override { return true; }
		void draw_bottom(ImVec2 size) override;

		bool dirty() const override { return m_Model && m_Model->dirty(); }
		bool save(std::string& error) override;
		void undo() override { if (m_Model) { m_Model->undo(); clamp_selection(); } }
		void redo() override { if (m_Model) { m_Model->redo(); clamp_selection(); } }
		bool can_undo() const override { return m_Model && m_Model->can_undo(); }
		bool can_redo() const override { return m_Model && m_Model->can_redo(); }

	private:
		void draw_skeleton();
		void draw_preview_controls();
		void draw_parameters();
		void draw_layers();
		void draw_states();
		void draw_state_details(const GraphLayer& layer, size_t state);
		void draw_transitions();
		void draw_transition_details(const GraphLayer& layer, size_t transition);
		void clamp_selection();
		// A text field that keeps its own buffer while it is being edited and says when the edit is done (Enter or leaving the field).
		bool name_field(const std::string& key, const std::string& current, std::string& result, float width = 150.0f);
		std::vector<std::string> parameter_names(bool floatsOnly) const;
		std::vector<std::string> state_names(const GraphLayer& layer) const;

		AssetEditorHost m_Host;
		std::filesystem::path m_File{};
		AssetRef m_Ref{};
		std::unique_ptr<GraphEditModel> m_Model{};
		Handle<SkeletonData> m_SkeletonHandle{};
		AssetRef m_LoadedSkeleton{};
		GraphSelection m_Selection{};
		GraphCanvas m_Canvas{};
		int m_CanvasLayer = -1;
		int m_LastTabLayer = 0;
		std::map<std::string, float> m_PreviewValues{}; // the parameters as the user set them in the preview: put back when the graph is cooked again
		bool m_ReapplyValues = true;
		bool m_ToolGesture = false; // a held slider is one undo step
		std::string m_Message{};

		std::string m_EditKey{};
		char m_EditBuffer[GraphEditModel::MAX_NAME_LENGTH + 16] = "";
		char m_NewParameter[GraphEditModel::MAX_NAME_LENGTH + 1] = "";
		int m_NewParameterType = 0;
		char m_NewState[GraphEditModel::MAX_NAME_LENGTH + 1] = "";
		char m_NewLayer[GraphEditModel::MAX_NAME_LENGTH + 1] = "";
	};
}
