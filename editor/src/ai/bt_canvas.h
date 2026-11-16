#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

#include <GraphEditor.h>

#include "bt_edit_model.h"

namespace wiwa
{
	// A Lua leaf script a tree can use: its name, asset guid and kind ("action" / "condition"), for the palette.
	struct BtLeafScript
	{
		std::string Name{};
		std::string Guid{};
		std::string Kind{};
	};

	// The node canvas of a behaviour tree: every node of the document with a slot in from its parent and one slot out per child (and a spare one while it
	// can take another), the parent-child links between them in child order. Adapts BtEditModel to the vendored GraphEditor widget like GraphCanvas does
	// for animation graphs. Dragging from an output slot to a node moves that node under it (at that child position); picking a link up removes the child
	// and everything below it (Undo brings it back); dragging nodes moves the selected ones (the layout is the node's "ui" parameter, one undo step per
	// drag); a node without a position is placed by a tidy top-down layout; right click opens a menu to add a child, move it among its siblings, rename
	// or delete it. Main thread, inside an ImGui window.
	class BtCanvas final : public ::GraphEditor::Delegate
	{
	public:
		static constexpr size_t MAX_SLOTS = 16;

		// Draws the tree in the space that is left in the window. `selected` is the selected node id (shared with the tool pane, empty: none).
		void draw(BtEditModel& model, std::string& selected, const std::vector<BtLeafScript>& leafScripts);
		// The view shows every node again on the next draw.
		void fit();
		// Writes the tidy layout into the "ui" parameters of every node (one undo step).
		static void auto_layout(BtEditModel& model);

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
		struct Op
		{
			enum class Kind { Reparent, Remove } What = Kind::Reparent;
			std::string Node{};
			std::string Parent{};
			size_t Index = 0;
		};

		void rebuild();
		static std::map<std::string, ImVec2> layout(const BehaviorTreeDoc& doc);
		static std::string summary(const BtNodeDesc& node);

		BtEditModel* m_Model = nullptr;
		std::string* m_Selected = nullptr;
		const std::vector<BtLeafScript>* m_Scripts = nullptr;

		::GraphEditor::Options m_Options{};
		::GraphEditor::ViewState m_View{};
		::GraphEditor::FitOnScreen m_Fit = ::GraphEditor::Fit_AllNodes;

		// rebuilt each frame
		std::vector<const BtNodeDesc*> m_Nodes{};
		std::map<std::string, size_t> m_Index{};
		std::map<std::string, ImVec2> m_Auto{};
		std::vector<size_t> m_OutCount{};
		std::vector<bool> m_HasInput{};
		std::vector<::GraphEditor::Link> m_Links{};
		std::vector<std::pair<std::string, size_t>> m_LinkChild{}; // per link: the parent's id and the child index it draws

		bool m_Gesture = false;
		ImVec2 m_PendingMove{ 0.0f, 0.0f };
		std::vector<Op> m_Ops{};
		std::set<std::string> m_SelectedSet{};
		std::string m_ContextNode{};
		bool m_OpenContext = false;
		ImVec2 m_ContextPosition{ 0.0f, 0.0f };
		std::string m_RenameBuffer{};
	};
}
