#include <editor_common.h>
#include "graph_canvas.h"

#include <imgui.h>

namespace wiwa
{
	namespace
	{
		constexpr float NODE_WIDTH = 130.0f;
		constexpr float NODE_HEIGHT = 58.0f;

		int state_index(const GraphLayer& layer, const std::string& name)
		{
			for (size_t i = 0; i < layer.States.size(); i++)
			{
				if (layer.States[i].Name == name)
				{
					return static_cast<int>(i);
				}
			}
			return -1;
		}
	}

	void GraphCanvas::fit()
	{
		m_Fit = ::GraphEditor::Fit_AllNodes;
		m_Selected.clear();
		::GraphEditor::GraphEditorClear();
	}

	void GraphCanvas::rebuild()
	{
		const GraphLayer& l = layer();
		const size_t nodes = FIRST_STATE + l.States.size();
		m_Names.assign(nodes, std::string{});
		m_Names[ENTRY] = "Entry";
		m_Names[ANY] = "Any state";
		for (size_t i = 0; i < l.States.size(); i++)
		{
			m_Names[FIRST_STATE + i] = l.States[i].Name;
		}

		std::vector<size_t> outUsed(nodes, 0);
		std::vector<size_t> inUsed(nodes, 0);
		m_Links.clear();
		m_LinkTransition.clear();
		for (size_t t = 0; t < l.Transitions.size(); t++)
		{
			const GraphTransition& transition = l.Transitions[t];
			const int to = state_index(l, transition.To);
			const int from = transition.From.empty() ? -2 : state_index(l, transition.From);
			if (to < 0 || from == -1)
			{
				continue; // a name the graph lacks (the model does not allow it): nothing to draw
			}
			const size_t source = from == -2 ? ANY : FIRST_STATE + static_cast<size_t>(from);
			const size_t destination = FIRST_STATE + static_cast<size_t>(to);
			m_Links.push_back(::GraphEditor::Link{ source, std::min(outUsed[source]++, MAX_SLOTS - 1), destination, std::min(inUsed[destination]++, MAX_SLOTS - 1) });
			m_LinkTransition.push_back(static_cast<int>(t));
		}
		const int defaultState = state_index(l, l.DefaultState);
		if (defaultState >= 0)
		{
			const size_t destination = FIRST_STATE + static_cast<size_t>(defaultState);
			m_Links.push_back(::GraphEditor::Link{ ENTRY, 0, destination, std::min(inUsed[destination]++, MAX_SLOTS - 1) });
			m_LinkTransition.push_back(-1);
		}

		m_InCount.assign(nodes, 0);
		m_OutCount.assign(nodes, 0);
		for (size_t n = 0; n < nodes; n++)
		{
			m_OutCount[n] = n == ENTRY ? 1 : std::min(outUsed[n] + 1, MAX_SLOTS);
			m_InCount[n] = n < FIRST_STATE ? 0 : std::min(inUsed[n] + 1, MAX_SLOTS);
		}
	}

	void GraphCanvas::draw(GraphEditModel& model, GraphSelection& selection, const std::string& activeState)
	{
		m_Model = &model;
		m_Selection = &selection;
		m_Layer = static_cast<size_t>(std::clamp(selection.Layer, 0, static_cast<int>(model.data().Layers.size()) - 1));
		m_Active = activeState;
		m_Ops.clear();
		m_PendingMove = ImVec2(0.0f, 0.0f);

		// the selection of the tool pane shows here, and the other way round
		const int statesCount = static_cast<int>(layer().States.size());
		std::erase_if(m_Selected, [&](int s) { return s >= statesCount; });
		if (selection.State >= 0 && !m_Selected.contains(selection.State))
		{
			m_Selected = { selection.State };
		}
		else if (selection.State < 0 && m_Selected.size() == 1)
		{
			m_Selected.clear();
		}

		rebuild();
		m_Origin = ImGui::GetCursorScreenPos();
		::GraphEditor::Show(*this, m_Options, m_View, true, &m_Fit);

		// the changes the widget asked for, now that it is not iterating over the graph
		if (m_Gesture && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			model.end_gesture();
			m_Gesture = false;
		}
		if (m_PendingMove.x != 0.0f || m_PendingMove.y != 0.0f)
		{
			if (!m_Gesture)
			{
				model.begin_gesture();
				m_Gesture = true;
			}
			for (const int s : m_Selected)
			{
				const GraphState& state = layer().States[static_cast<size_t>(s)];
				model.set_state_position(m_Layer, static_cast<size_t>(s), state.Position + glm::vec2(m_PendingMove.x, m_PendingMove.y));
			}
		}
		for (const Op& op : m_Ops)
		{
			switch (op.What)
			{
			case Op::Kind::AddTransition:
			{
				const GraphLayer& l = layer();
				const std::string from = op.From == ANY ? std::string{} : l.States[op.From - FIRST_STATE].Name;
				if (model.add_transition(m_Layer, from, l.States[op.To - FIRST_STATE].Name))
				{
					selection.Transition = static_cast<int>(layer().Transitions.size()) - 1;
					selection.State = -1;
					m_Selected.clear();
				}
				break;
			}
			case Op::Kind::SetDefault:
				model.set_default_state(m_Layer, op.To - FIRST_STATE);
				break;
			case Op::Kind::DeleteTransition:
				if (model.delete_transition(m_Layer, op.Index) && selection.Transition >= static_cast<int>(layer().Transitions.size()))
				{
					selection.Transition = -1;
				}
				break;
			}
		}

		if (m_Selected.size() == 1)
		{
			selection.State = *m_Selected.begin();
			selection.Transition = -1;
		}
		else if (m_Selected.empty() && selection.State >= 0)
		{
			selection.State = -1;
		}

		if (m_OpenContext)
		{
			ImGui::OpenPopup("##graphcanvascontext");
			m_OpenContext = false;
		}
		if (ImGui::BeginPopup("##graphcanvascontext"))
		{
			if (m_ContextNode >= FIRST_STATE && m_ContextNode - FIRST_STATE < layer().States.size())
			{
				const size_t state = m_ContextNode - FIRST_STATE;
				if (ImGui::MenuItem("Make default state"))
				{
					model.set_default_state(m_Layer, state);
				}
				if (ImGui::MenuItem("Delete state"))
				{
					model.delete_state(m_Layer, state);
					m_Selected.clear();
					selection.State = -1;
				}
				ImGui::Separator();
			}
			if (ImGui::MenuItem("Add state here"))
			{
				std::string name = "State";
				for (int n = 2; state_index(layer(), name) >= 0; n++)
				{
					name = "State " + std::to_string(n);
				}
				const float factor = m_View.mFactor > 0.0f ? m_View.mFactor : 1.0f;
				const ImVec2 offset(m_Origin.x + m_View.mPosition.x * factor, m_Origin.y + m_View.mPosition.y * factor);
				const glm::vec2 position((m_ContextPosition.x - offset.x) / factor, (m_ContextPosition.y - offset.y) / factor);
				model.begin_gesture();
				if (model.add_state(m_Layer, name))
				{
					model.set_state_position(m_Layer, layer().States.size() - 1, position);
				}
				model.end_gesture();
			}
			ImGui::EndPopup();
		}
	}

	bool GraphCanvas::AllowedLink(::GraphEditor::NodeIndex destination, ::GraphEditor::NodeIndex source)
	{
		return destination >= FIRST_STATE && destination < FIRST_STATE + layer().States.size() && source != destination && source < FIRST_STATE + layer().States.size();
	}

	void GraphCanvas::SelectNode(::GraphEditor::NodeIndex node, bool selected)
	{
		if (node < FIRST_STATE)
		{
			return;
		}
		const int state = static_cast<int>(node - FIRST_STATE);
		if (selected)
		{
			m_Selected.insert(state);
		}
		else
		{
			m_Selected.erase(state);
		}
	}

	void GraphCanvas::MoveSelectedNodes(const ImVec2 delta)
	{
		m_PendingMove.x += delta.x;
		m_PendingMove.y += delta.y;
	}

	void GraphCanvas::AddLink(::GraphEditor::NodeIndex sourceNode, ::GraphEditor::SlotIndex, ::GraphEditor::NodeIndex destinationNode, ::GraphEditor::SlotIndex)
	{
		if (!AllowedLink(destinationNode, sourceNode))
		{
			return;
		}
		Op op;
		op.What = sourceNode == ENTRY ? Op::Kind::SetDefault : Op::Kind::AddTransition;
		op.From = sourceNode;
		op.To = destinationNode;
		m_Ops.push_back(op);
	}

	void GraphCanvas::DelLink(::GraphEditor::LinkIndex link)
	{
		if (link < m_LinkTransition.size() && m_LinkTransition[link] >= 0)
		{
			Op op;
			op.What = Op::Kind::DeleteTransition;
			op.Index = static_cast<size_t>(m_LinkTransition[link]);
			m_Ops.push_back(op);
		}
	}

	void GraphCanvas::CustomDraw(ImDrawList* drawList, ImRect rectangle, ::GraphEditor::NodeIndex node)
	{
		if (node < FIRST_STATE || node - FIRST_STATE >= layer().States.size())
		{
			return;
		}
		const GraphState& state = layer().States[node - FIRST_STATE];
		drawList->PushClipRect(rectangle.Min, rectangle.Max, true);
		char text[64];
		if (state.Body == GraphBodyKind::Blend)
		{
			std::snprintf(text, sizeof(text), "blend %zu%s", state.Samples.size(), state.BlendY.empty() ? "" : " (2D)");
		}
		else
		{
			std::snprintf(text, sizeof(text), "%s", state.Clip.valid() ? "clip" : "no clip");
		}
		drawList->AddText(ImVec2(rectangle.Min.x + 2.0f, rectangle.Min.y), IM_COL32(200, 200, 200, 255), text);
		if (state.Name == m_Active)
		{
			drawList->AddRectFilled(rectangle.Min, ImVec2(rectangle.Max.x, rectangle.Min.y + 3.0f), IM_COL32(80, 220, 110, 255));
			drawList->AddText(ImVec2(rectangle.Min.x + 2.0f, rectangle.Min.y + 14.0f), IM_COL32(120, 240, 150, 255), "playing");
		}
		else if (state.Name == layer().DefaultState)
		{
			drawList->AddText(ImVec2(rectangle.Min.x + 2.0f, rectangle.Min.y + 14.0f), IM_COL32(150, 150, 150, 255), "default");
		}
		drawList->PopClipRect();
	}

	void GraphCanvas::RightClick(::GraphEditor::NodeIndex node, ::GraphEditor::SlotIndex, ::GraphEditor::SlotIndex)
	{
		m_ContextNode = node == static_cast<::GraphEditor::NodeIndex>(-1) ? 0 : node;
		m_ContextPosition = ImGui::GetIO().MousePos;
		m_OpenContext = true;
	}

	const size_t GraphCanvas::GetTemplateCount()
	{
		return 1 + MAX_SLOTS + MAX_SLOTS * MAX_SLOTS;
	}

	const ::GraphEditor::Template GraphCanvas::GetTemplate(::GraphEditor::TemplateIndex index)
	{
		::GraphEditor::Template result{};
		result.mBackgroundColor = IM_COL32(60, 60, 64, 255);
		result.mBackgroundColorOver = IM_COL32(75, 75, 80, 255);
		if (index == 0)
		{
			result.mHeaderColor = IM_COL32(40, 120, 60, 255);
			result.mOutputCount = 1;
		}
		else if (index <= MAX_SLOTS)
		{
			result.mHeaderColor = IM_COL32(50, 90, 150, 255);
			result.mOutputCount = static_cast<ImU8>(index);
		}
		else
		{
			const size_t k = index - 1 - MAX_SLOTS;
			result.mHeaderColor = IM_COL32(110, 80, 40, 255);
			result.mInputCount = static_cast<ImU8>(k / MAX_SLOTS + 1);
			result.mOutputCount = static_cast<ImU8>(k % MAX_SLOTS + 1);
		}
		return result;
	}

	const size_t GraphCanvas::GetNodeCount()
	{
		return FIRST_STATE + layer().States.size();
	}

	const ::GraphEditor::Node GraphCanvas::GetNode(::GraphEditor::NodeIndex index)
	{
		::GraphEditor::Node node{};
		node.mName = m_Names[index].c_str();
		if (index < FIRST_STATE)
		{
			// the two fixed nodes sit left of the states
			float left = 0.0f;
			for (const GraphState& state : layer().States)
			{
				left = std::min(left, state.Position.x);
			}
			const float x = left - 190.0f;
			const float y = index == ENTRY ? 0.0f : 90.0f;
			node.mTemplateIndex = index == ENTRY ? 0 : std::min(m_OutCount[index], MAX_SLOTS);
			node.mRect = ImRect(ImVec2(x, y), ImVec2(x + 110.0f, y + 40.0f));
			node.mSelected = false;
			return node;
		}
		const GraphState& state = layer().States[index - FIRST_STATE];
		node.mTemplateIndex = 1 + MAX_SLOTS + (m_InCount[index] - 1) * MAX_SLOTS + (m_OutCount[index] - 1);
		node.mRect = ImRect(ImVec2(state.Position.x, state.Position.y), ImVec2(state.Position.x + NODE_WIDTH, state.Position.y + NODE_HEIGHT));
		node.mSelected = m_Selected.contains(static_cast<int>(index - FIRST_STATE));
		return node;
	}

	const size_t GraphCanvas::GetLinkCount()
	{
		return m_Links.size();
	}

	const ::GraphEditor::Link GraphCanvas::GetLink(::GraphEditor::LinkIndex index)
	{
		return m_Links[index];
	}
}
