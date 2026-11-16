#include <editor_common.h>
#include "bt_canvas.h"

#include <algorithm>

#include <imgui.h>

namespace wiwa
{
	namespace
	{
		constexpr float NODE_WIDTH = 150.0f;
		constexpr float NODE_HEIGHT = 56.0f;
		constexpr float GAP_X = 175.0f;
		constexpr float GAP_Y = 110.0f;

		enum class Category : int { Composite = 0, Decorator = 1, Leaf = 2, Lua = 3 };

		Category category_of(const std::string& type)
		{
			if (type == "Sequence" || type == "Selector" || type == "Parallel") return Category::Composite;
			if (type == "Inverter" || type == "Succeeder" || type == "Repeat" || type == "Cooldown" || type == "Guard") return Category::Decorator;
			if (type == "LuaAction" || type == "LuaCondition") return Category::Lua;
			return Category::Leaf;
		}

		bool has_ui(const BtNodeDesc& node, ImVec2& out)
		{
			const auto ui = node.Params.find("ui");
			if (ui == node.Params.end() || !ui->is_object() || !ui->contains("x") || !ui->contains("y") || !(*ui)["x"].is_number() || !(*ui)["y"].is_number())
			{
				return false;
			}
			out = ImVec2((*ui)["x"].get<float>(), (*ui)["y"].get<float>());
			return true;
		}

		// How many children the node can still take (-1: any number).
		int room(const BtNodeDesc& node)
		{
			const BtNodeSpec* spec = bt_find_node_spec(node.Type);
			if (spec == nullptr || spec->Leaf())
			{
				return 0;
			}
			return spec->MaxChildren < 0 ? -1 : std::max(spec->MaxChildren - static_cast<int>(node.Children.size()), 0);
		}

		float place(const BehaviorTreeDoc& doc, const std::string& id, int depth, float& nextX, std::map<std::string, ImVec2>& out, std::set<std::string>& seen)
		{
			const BtNodeDesc* node = doc.find(id);
			if (node == nullptr || !seen.insert(id).second)
			{
				return nextX;
			}
			float x;
			if (node->Children.empty())
			{
				x = nextX;
				nextX += GAP_X;
			}
			else
			{
				float first = 0.0f;
				float last = 0.0f;
				for (size_t i = 0; i < node->Children.size(); i++)
				{
					const float child = place(doc, node->Children[i], depth + 1, nextX, out, seen);
					if (i == 0) { first = child; }
					last = child;
				}
				x = (first + last) * 0.5f;
			}
			out[id] = ImVec2(x, static_cast<float>(depth) * GAP_Y);
			return x;
		}
	}

	std::map<std::string, ImVec2> BtCanvas::layout(const BehaviorTreeDoc& doc)
	{
		std::map<std::string, ImVec2> positions;
		std::set<std::string> seen;
		float nextX = 0.0f;
		place(doc, doc.Root, 0, nextX, positions, seen);
		// nodes the root does not reach stand in a row below
		float x = 0.0f;
		for (const BtNodeDesc& node : doc.Nodes)
		{
			if (!positions.contains(node.Id))
			{
				positions[node.Id] = ImVec2(x, 4.0f * GAP_Y);
				x += GAP_X;
			}
		}
		return positions;
	}

	void BtCanvas::auto_layout(BtEditModel& model)
	{
		const std::map<std::string, ImVec2> positions = layout(model.doc());
		model.begin_gesture();
		for (const auto& [id, position] : positions)
		{
			model.set_param(id, "ui", nlohmann::json{ { "x", position.x }, { "y", position.y } });
		}
		model.end_gesture();
	}

	void BtCanvas::fit()
	{
		m_Fit = ::GraphEditor::Fit_AllNodes;
		m_SelectedSet.clear();
		::GraphEditor::GraphEditorClear();
	}

	std::string BtCanvas::summary(const BtNodeDesc& node)
	{
		const auto text = [&](const char* name) { const auto it = node.Params.find(name); return it != node.Params.end() && it->is_string() ? it->get<std::string>() : std::string(); };
		const auto number = [&](const char* name) { const auto it = node.Params.find(name); return it != node.Params.end() && it->is_number() ? it->get<double>() : -1.0; };
		char buffer[96];
		if (node.Type == "Wait")
		{
			if (number("seconds") >= 0.0) { std::snprintf(buffer, sizeof(buffer), "%.2f s", number("seconds")); }
			else { std::snprintf(buffer, sizeof(buffer), "%.2f - %.2f s", std::max(number("min"), 0.0), std::max(number("max"), 0.0)); }
			return buffer;
		}
		if (node.Type == "Guard" || node.Type == "CheckBlackboard")
		{
			return text("key") + " " + text("op");
		}
		if (node.Type == "SetBlackboard") return text("key");
		if (node.Type == "MoveTo")
		{
			const auto target = node.Params.find("target");
			return target != node.Params.end() && target->is_string() ? target->get<std::string>() : std::string("to a point");
		}
		if (node.Type == "PlayAnimation") return text("clip");
		if (node.Type == "Repeat") { std::snprintf(buffer, sizeof(buffer), number("count") > 0 ? "%d times" : "for ever", static_cast<int>(number("count"))); return buffer; }
		if (node.Type == "Cooldown") { std::snprintf(buffer, sizeof(buffer), "%.1f s", number("seconds")); return buffer; }
		if ((node.Type == "Sequence" || node.Type == "Selector") && node.Params.contains("reactive") && node.Params["reactive"].is_boolean() && node.Params["reactive"].get<bool>()) return "reactive";
		if (node.Type == "Log") return text("message");
		return {};
	}

	void BtCanvas::rebuild()
	{
		const BehaviorTreeDoc& doc = m_Model->doc();
		m_Nodes.clear();
		m_Index.clear();
		for (const BtNodeDesc& node : doc.Nodes)
		{
			m_Index[node.Id] = m_Nodes.size();
			m_Nodes.push_back(&node);
		}
		m_Auto = layout(doc);

		m_OutCount.assign(m_Nodes.size(), 0);
		m_HasInput.assign(m_Nodes.size(), false);
		m_Links.clear();
		m_LinkChild.clear();
		for (size_t n = 0; n < m_Nodes.size(); n++)
		{
			const BtNodeDesc& node = *m_Nodes[n];
			m_HasInput[n] = node.Id != doc.Root;
			for (size_t c = 0; c < node.Children.size(); c++)
			{
				const auto child = m_Index.find(node.Children[c]);
				if (child == m_Index.end())
				{
					continue;
				}
				m_Links.push_back(::GraphEditor::Link{ n, std::min(c, MAX_SLOTS - 1), child->second, 0 });
				m_LinkChild.emplace_back(node.Id, c);
			}
			const int spare = room(node);
			m_OutCount[n] = std::min(node.Children.size() + (spare != 0 ? 1u : 0u), MAX_SLOTS);
		}
	}

	void BtCanvas::draw(BtEditModel& model, std::string& selected, const std::vector<BtLeafScript>& leafScripts)
	{
		m_Model = &model;
		m_Selected = &selected;
		m_Scripts = &leafScripts;
		m_Ops.clear();
		m_PendingMove = ImVec2(0.0f, 0.0f);

		// the selection of the tool pane shows here, and the other way round
		if (!selected.empty() && model.doc().find(selected) == nullptr)
		{
			selected.clear();
		}
		if (!selected.empty() && !m_SelectedSet.contains(selected))
		{
			m_SelectedSet = { selected };
		}
		else if (selected.empty() && m_SelectedSet.size() == 1)
		{
			m_SelectedSet.clear();
		}
		std::erase_if(m_SelectedSet, [&](const std::string& id) { return model.doc().find(id) == nullptr; });

		rebuild();
		::GraphEditor::Show(*this, m_Options, m_View, true, &m_Fit);

		// the changes the widget asked for, now that it is not iterating over the tree
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
			for (const std::string& id : m_SelectedSet)
			{
				const BtNodeDesc* node = model.doc().find(id);
				if (node == nullptr)
				{
					continue;
				}
				ImVec2 at(0.0f, 0.0f);
				if (!has_ui(*node, at))
				{
					const auto fallback = m_Auto.find(id);
					at = fallback != m_Auto.end() ? fallback->second : ImVec2(0.0f, 0.0f);
				}
				model.set_param(id, "ui", nlohmann::json{ { "x", at.x + m_PendingMove.x }, { "y", at.y + m_PendingMove.y } });
			}
		}
		for (const Op& op : m_Ops)
		{
			if (op.What == Op::Kind::Reparent)
			{
				model.move_node(op.Node, op.Parent, op.Index);
			}
			else
			{
				model.remove_node(op.Node);
				m_SelectedSet.erase(op.Node);
			}
		}

		if (m_SelectedSet.size() == 1)
		{
			selected = *m_SelectedSet.begin();
		}
		else if (m_SelectedSet.empty())
		{
			selected.clear();
		}

		if (m_OpenContext)
		{
			ImGui::OpenPopup("##btcanvascontext");
			m_OpenContext = false;
		}

		// adds a node under `parent` (empty: as the root) and places it under the parent
		const auto add = [&](const std::string& type, const std::string& parent, const BtLeafScript* script)
		{
			model.begin_gesture();
			const std::string id = model.add_node(type, parent);
			if (!id.empty())
			{
				if (script != nullptr)
				{
					model.set_param(id, "script", script->Guid);
					model.rename_node(id, script->Name);
				}
				ImVec2 at(0.0f, 0.0f);
				if (!parent.empty())
				{
					const BtNodeDesc* owner = model.doc().find(parent);
					if (owner != nullptr)
					{
						ImVec2 base = m_Auto.contains(parent) ? m_Auto[parent] : ImVec2(0.0f, 0.0f);
						has_ui(*owner, base);
						at = ImVec2(base.x + GAP_X * static_cast<float>(owner->Children.size() - 1), base.y + GAP_Y);
					}
				}
				model.set_param(id, "ui", nlohmann::json{ { "x", at.x }, { "y", at.y } });
				m_SelectedSet = { id };
				selected = id;
			}
			model.end_gesture();
		};
		const auto type_menu = [&](const std::string& parent, bool leavesOnly)
		{
			const char* names[] = { "Composites", "Decorators", "Leaves" };
			const Category groups[] = { Category::Composite, Category::Decorator, Category::Leaf };
			for (int g = 0; g < 3; g++)
			{
				if (leavesOnly && groups[g] != Category::Leaf)
				{
					continue;
				}
				ImGui::SeparatorText(names[g]);
				for (const BtNodeSpec& spec : bt_builtin_node_specs())
				{
					if (category_of(spec.Type) == groups[g] && ImGui::MenuItem(spec.Type.c_str()))
					{
						add(spec.Type, parent, nullptr);
					}
				}
			}
			if (!m_Scripts->empty())
			{
				ImGui::SeparatorText("Lua leaves");
				for (const BtLeafScript& script : *m_Scripts)
				{
					const std::string label = script.Name + (script.Kind == "condition" ? " (condition)" : " (action)");
					if (ImGui::MenuItem(label.c_str()))
					{
						add(script.Kind == "condition" ? "LuaCondition" : "LuaAction", parent, &script);
					}
				}
			}
		};

		bool openRename = false;
		if (ImGui::BeginPopup("##btcanvascontext"))
		{
			const BtNodeDesc* node = m_ContextNode.empty() ? nullptr : model.doc().find(m_ContextNode);
			if (model.doc().Nodes.empty())
			{
				if (ImGui::BeginMenu("Add root"))
				{
					type_menu({}, false);
					ImGui::EndMenu();
				}
			}
			else if (node != nullptr)
			{
				const std::string nodeId = node->Id; // a copy: adding a node can move the document's nodes
				if (room(*node) != 0 && ImGui::BeginMenu("Add child"))
				{
					type_menu(nodeId, false);
					ImGui::EndMenu();
				}
				node = model.doc().find(nodeId);
				if (node == nullptr)
				{
					ImGui::EndPopup();
					return;
				}
				const bool isRoot = node->Id == model.doc().Root;
				const BtNodeDesc* owner = nullptr;
				size_t index = 0;
				for (const BtNodeDesc& candidate : model.doc().Nodes)
				{
					const auto found = std::find(candidate.Children.begin(), candidate.Children.end(), node->Id);
					if (found != candidate.Children.end())
					{
						owner = &candidate;
						index = static_cast<size_t>(found - candidate.Children.begin());
					}
				}
				if (ImGui::MenuItem("Move earlier", nullptr, false, owner != nullptr && index > 0))
				{
					model.move_node(node->Id, owner->Id, index - 1);
				}
				if (ImGui::MenuItem("Move later", nullptr, false, owner != nullptr && index + 1 < owner->Children.size()))
				{
					model.move_node(node->Id, owner->Id, index + 1);
				}
				if (ImGui::MenuItem("Rename..."))
				{
					m_RenameBuffer = node->Name;
					openRename = true;
				}
				if (ImGui::MenuItem("Delete node and what is below it", nullptr, false, !isRoot))
				{
					model.remove_node(node->Id);
					m_SelectedSet.erase(m_ContextNode);
					selected.clear();
				}
			}
			ImGui::EndPopup();
		}
		if (openRename)
		{
			ImGui::OpenPopup("##btrename");
		}
		if (ImGui::BeginPopup("##btrename"))
		{
			char buffer[96];
			std::snprintf(buffer, sizeof(buffer), "%s", m_RenameBuffer.c_str());
			ImGui::SetKeyboardFocusHere();
			if (ImGui::InputText("##name", buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue))
			{
				model.rename_node(m_ContextNode, buffer);
				ImGui::CloseCurrentPopup();
			}
			m_RenameBuffer = buffer;
			ImGui::EndPopup();
		}
	}

	bool BtCanvas::AllowedLink(::GraphEditor::NodeIndex destination, ::GraphEditor::NodeIndex source)
	{
		return destination < m_Nodes.size() && source < m_Nodes.size() && source != destination && m_HasInput[destination] && room(*m_Nodes[source]) != 0;
	}

	void BtCanvas::SelectNode(::GraphEditor::NodeIndex node, bool selected)
	{
		if (node >= m_Nodes.size())
		{
			return;
		}
		if (selected)
		{
			m_SelectedSet.insert(m_Nodes[node]->Id);
		}
		else
		{
			m_SelectedSet.erase(m_Nodes[node]->Id);
		}
	}

	void BtCanvas::MoveSelectedNodes(const ImVec2 delta)
	{
		m_PendingMove.x += delta.x;
		m_PendingMove.y += delta.y;
	}

	void BtCanvas::AddLink(::GraphEditor::NodeIndex sourceNode, ::GraphEditor::SlotIndex sourceSlot, ::GraphEditor::NodeIndex destinationNode, ::GraphEditor::SlotIndex)
	{
		if (!AllowedLink(destinationNode, sourceNode))
		{
			return;
		}
		Op op;
		op.What = Op::Kind::Reparent;
		op.Node = m_Nodes[destinationNode]->Id;
		op.Parent = m_Nodes[sourceNode]->Id;
		op.Index = sourceSlot;
		m_Ops.push_back(op);
	}

	void BtCanvas::DelLink(::GraphEditor::LinkIndex link)
	{
		// The widget picks a link up when the user drags a connected input, and calls this before AddLink when the node is dropped on another parent:
		// removing the child here would delete a subtree for a re-link (and leave the widget with a stale node index). Moving a node is AddLink; removing
		// one is the context menu.
		(void)link;
	}

	void BtCanvas::CustomDraw(ImDrawList* drawList, ImRect rectangle, ::GraphEditor::NodeIndex node)
	{
		if (node >= m_Nodes.size())
		{
			return;
		}
		const BtNodeDesc& desc = *m_Nodes[node];
		drawList->PushClipRect(rectangle.Min, rectangle.Max, true);
		drawList->AddText(ImVec2(rectangle.Min.x + 2.0f, rectangle.Min.y), IM_COL32(200, 200, 200, 255), desc.Name.empty() ? "" : desc.Type.c_str());
		const std::string detail = summary(desc);
		if (!detail.empty())
		{
			drawList->AddText(ImVec2(rectangle.Min.x + 2.0f, rectangle.Min.y + 14.0f), IM_COL32(150, 190, 230, 255), detail.c_str());
		}
		drawList->PopClipRect();
	}

	void BtCanvas::RightClick(::GraphEditor::NodeIndex node, ::GraphEditor::SlotIndex, ::GraphEditor::SlotIndex)
	{
		m_ContextNode = node < m_Nodes.size() ? m_Nodes[node]->Id : std::string();
		m_ContextPosition = ImGui::GetIO().MousePos;
		m_OpenContext = true;
	}

	const size_t BtCanvas::GetTemplateCount()
	{
		return 4 * 2 * (MAX_SLOTS + 1);
	}

	const ::GraphEditor::Template BtCanvas::GetTemplate(::GraphEditor::TemplateIndex index)
	{
		::GraphEditor::Template result{};
		const size_t perGroup = MAX_SLOTS + 1;
		const size_t group = index / perGroup;
		const Category category = static_cast<Category>(group / 2);
		result.mInputCount = static_cast<ImU8>(group % 2);
		result.mOutputCount = static_cast<ImU8>(index % perGroup);
		result.mBackgroundColor = IM_COL32(60, 60, 64, 255);
		result.mBackgroundColorOver = IM_COL32(75, 75, 80, 255);
		switch (category)
		{
		case Category::Composite: result.mHeaderColor = IM_COL32(50, 90, 150, 255); break;
		case Category::Decorator: result.mHeaderColor = IM_COL32(110, 70, 150, 255); break;
		case Category::Leaf: result.mHeaderColor = IM_COL32(50, 125, 70, 255); break;
		case Category::Lua: result.mHeaderColor = IM_COL32(170, 110, 40, 255); break;
		}
		return result;
	}

	const size_t BtCanvas::GetNodeCount()
	{
		return m_Nodes.size();
	}

	const ::GraphEditor::Node BtCanvas::GetNode(::GraphEditor::NodeIndex index)
	{
		const BtNodeDesc& desc = *m_Nodes[index];
		::GraphEditor::Node node{};
		node.mName = desc.Name.empty() ? desc.Type.c_str() : desc.Name.c_str();
		ImVec2 at(0.0f, 0.0f);
		if (!has_ui(desc, at))
		{
			const auto fallback = m_Auto.find(desc.Id);
			at = fallback != m_Auto.end() ? fallback->second : ImVec2(0.0f, 0.0f);
		}
		node.mRect = ImRect(at, ImVec2(at.x + NODE_WIDTH, at.y + NODE_HEIGHT));
		node.mSelected = m_SelectedSet.contains(desc.Id);
		const size_t group = static_cast<size_t>(category_of(desc.Type)) * 2 + (m_HasInput[index] ? 1 : 0);
		node.mTemplateIndex = group * (MAX_SLOTS + 1) + std::min(m_OutCount[index], MAX_SLOTS);
		return node;
	}

	const size_t BtCanvas::GetLinkCount()
	{
		return m_Links.size();
	}

	const ::GraphEditor::Link BtCanvas::GetLink(::GraphEditor::LinkIndex index)
	{
		return m_Links[index];
	}
}
