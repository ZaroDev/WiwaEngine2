#pragma once

#include <set>
#include <string>
#include <vector>

#include <GraphEditor.h>

#include "graph_edit_model.h"

namespace wiwa
{
	// The node canvas of one layer of a graph: the states as nodes (with Entry, which points at the default state, and Any, the source of the
	// transitions from any state), the transitions as links. Adapts GraphEditModel to the vendored GraphEditor widget: the widget gives each slot one
	// link, and a state has many transitions, so every node gets one slot per link and a spare one (up to MAX_SLOTS) to start a new link from or
	// drop one on. Dragging from a slot to a state adds a transition (from Entry: sets the default state), picking a link up deletes it (Undo brings
	// it back), dragging nodes moves the selected states (one undo step per drag), right click opens a menu. Main thread, inside an ImGui window.
	class GraphCanvas final : public ::GraphEditor::Delegate
	{
	public:
		static constexpr size_t MAX_SLOTS = 12;

		// Draws the layer in the space that is left in the window. activeState: the state the preview plays (empty: none), drawn highlighted.
		void draw(GraphEditModel& model, GraphSelection& selection, const std::string& activeState);
		// The view shows every node again on the next draw (another layer, a new file).
		void fit();

		// ::GraphEditor::Delegate
		bool AllowedLink(::GraphEditor::NodeIndex destination, ::GraphEditor::NodeIndex source) override;
		void SelectNode(::GraphEditor::NodeIndex node, bool selected) override;
		void MoveSelectedNodes(const ImVec2 delta) override;
		void AddLink(::GraphEditor::NodeIndex sourceNode, ::GraphEditor::SlotIndex sourceSlot, ::GraphEditor::NodeIndex destinationNode, ::GraphEditor::SlotIndex destinationSlot) override;
		void DelLink(::GraphEditor::LinkIndex link) override;
		void CustomDraw(ImDrawList* drawList, ImRect rectangle, ::GraphEditor::NodeIndex node) override;
		void RightClick(::GraphEditor::NodeIndex node, ::GraphEditor::SlotIndex slotInput, ::GraphEditor::SlotIndex slotOutput) override;
		const size_t GetTemplateCount() override;
		const ::GraphEditor::Template GetTemplate(::GraphEditor::TemplateIndex index) override;
		const size_t GetNodeCount() override;
		const ::GraphEditor::Node GetNode(::GraphEditor::NodeIndex index) override;
		const size_t GetLinkCount() override;
		const ::GraphEditor::Link GetLink(::GraphEditor::LinkIndex index) override;

	private:
		static constexpr size_t ENTRY = 0;
		static constexpr size_t ANY = 1;
		static constexpr size_t FIRST_STATE = 2;

		struct Op
		{
			enum class Kind { AddTransition, SetDefault, DeleteTransition } What = Kind::AddTransition;
			size_t From = 0;
			size_t To = 0;
			size_t Index = 0;
		};

		void rebuild();
		const GraphLayer& layer() const { return m_Model->data().Layers[m_Layer]; }

		GraphEditModel* m_Model = nullptr;
		GraphSelection* m_Selection = nullptr;
		size_t m_Layer = 0;
		std::string m_Active{};

		::GraphEditor::Options m_Options{};
		::GraphEditor::ViewState m_View{};
		::GraphEditor::FitOnScreen m_Fit = ::GraphEditor::Fit_AllNodes;

		// rebuilt each frame
		std::vector<std::string> m_Names{};
		std::vector<size_t> m_InCount{};
		std::vector<size_t> m_OutCount{};
		std::vector<::GraphEditor::Link> m_Links{};
		std::vector<int> m_LinkTransition{}; // per link: the transition it draws, -1 for the Entry link

		std::set<int> m_Selected{};
		bool m_Gesture = false;
		ImVec2 m_PendingMove{ 0.0f, 0.0f };
		std::vector<Op> m_Ops{};
		ImVec2 m_Origin{ 0.0f, 0.0f };
		size_t m_ContextNode = 0;
		bool m_OpenContext = false;
		ImVec2 m_ContextPosition{ 0.0f, 0.0f };
	};
}
