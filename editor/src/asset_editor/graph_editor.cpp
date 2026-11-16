#include <editor_common.h>
#include "graph_editor.h"

#include <algorithm>

#include <imgui.h>

#include <assets/asset_manager.h>
#include <assets/native_anim_files.h>
#include <core/engine.h>

#include <asset_pipeline/asset_database.h>
#include <core/file_io.h>
#include <asset_pipeline/file_utils.h>
#include <panels/asset_field.h>

#include "asset_preview.h"

using wiwa::LogLevel; // for WI_LOG
#define WI_ASSET_LOG(level, ...) WI_LOG(level, "AssetEditor", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		ImVec4 error_color() { return ImVec4(1.0f, 0.45f, 0.4f, 1.0f); }

		std::optional<AssetRef> asset_at(const std::string& path)
		{
			AssetManager* assets = Engine::get().get_module<AssetManager>();
			return assets != nullptr ? assets->find_asset(path) : std::nullopt;
		}

		// The asset of a file dropped on a slot that wants an extension; a message when it is not one.
		bool dropped_asset(const std::string& path, const char* extension, const char* what, AssetRef& out, std::string& message)
		{
			const std::optional<AssetRef> ref = std::filesystem::path(path).extension() == extension ? asset_at(path) : std::nullopt;
			if (!ref)
			{
				message = std::string("That is not ") + what + " the project knows";
				return false;
			}
			out = *ref;
			return true;
		}

		const char* PARAM_TYPES[] = { "float", "int", "bool", "trigger" };
		const GraphParamType PARAM_TYPE_VALUES[] = { GraphParamType::Float, GraphParamType::Int, GraphParamType::Bool, GraphParamType::Trigger };

		struct OpName
		{
			GraphCompareOp Op;
			const char* Name;
		};
		const OpName NUMBER_OPS[] = { { GraphCompareOp::Greater, ">" }, { GraphCompareOp::Less, "<" }, { GraphCompareOp::GreaterEqual, ">=" },
			{ GraphCompareOp::LessEqual, "<=" }, { GraphCompareOp::Equal, "==" }, { GraphCompareOp::NotEqual, "!=" } };
		const OpName BOOL_OPS[] = { { GraphCompareOp::IsTrue, "is true" }, { GraphCompareOp::IsFalse, "is false" } };
		const OpName TRIGGER_OPS[] = { { GraphCompareOp::Fired, "fired" } };

		std::span<const OpName> ops_for(GraphParamType type)
		{
			switch (type)
			{
			case GraphParamType::Bool: return BOOL_OPS;
			case GraphParamType::Trigger: return TRIGGER_OPS;
			default: return NUMBER_OPS;
			}
		}

		const GraphParameter* find_parameter(const AnimationGraphData& graph, const std::string& name)
		{
			for (const GraphParameter& parameter : graph.Parameters)
			{
				if (parameter.Name == name)
				{
					return &parameter;
				}
			}
			return nullptr;
		}

		// A combo of names; allowNone adds an entry for the empty string. Returns true when the choice changed (value then holds it).
		bool name_combo(const char* label, std::string& value, const std::vector<std::string>& names, const char* none, float width = 140.0f)
		{
			bool changed = false;
			ImGui::SetNextItemWidth(width);
			if (ImGui::BeginCombo(label, value.empty() ? (none != nullptr ? none : "") : value.c_str()))
			{
				if (none != nullptr && ImGui::Selectable(none, value.empty()))
				{
					value.clear();
					changed = true;
				}
				for (const std::string& name : names)
				{
					if (ImGui::Selectable(name.c_str(), name == value))
					{
						value = name;
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
			return changed;
		}
	}

	bool GraphEditor::load(const std::filesystem::path& file, std::string& error)
	{
		const std::optional<std::vector<std::byte>> bytes = read_binary_file(file);
		if (!bytes)
		{
			error = "cannot read " + file.string();
			return false;
		}
		// not validated: a graph that is being made (a state without its clip) opens; Save asks for the rest
		std::optional<AnimationGraphData> graph;
		try
		{
			graph = read_graph_file(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), error, false);
		}
		catch (const std::exception& exception)
		{
			error = std::string("the file has a value of the wrong kind: ") + exception.what();
			return false;
		}
		if (!graph)
		{
			return false;
		}
		m_File = file;
		// a hand-edited file may have no layer, or a layer without states or with a default that is not one: make it something the tools can show
		if (graph->Layers.empty())
		{
			graph->Layers.push_back(GraphLayer{});
			graph->Layers.back().Name = "Base";
		}
		for (GraphLayer& layer : graph->Layers)
		{
			if (layer.States.empty())
			{
				layer.States.push_back(GraphState{});
				layer.States.back().Name = "State";
			}
			const bool known = std::any_of(layer.States.begin(), layer.States.end(), [&](const GraphState& state) { return state.Name == layer.DefaultState; });
			if (!known)
			{
				layer.DefaultState = layer.States.front().Name;
			}
		}
		graph->Layers.front().Mode = GraphLayerMode::Base;
		for (size_t i = 1; i < graph->Layers.size(); i++)
		{
			if (graph->Layers[i].Mode == GraphLayerMode::Base)
			{
				graph->Layers[i].Mode = GraphLayerMode::Override;
			}
		}
		if (graph->Skeleton.valid())
		{
			graph->Skeleton = m_Host.Database.redirected(graph->Skeleton);
		}
		m_Model = std::make_unique<GraphEditModel>(std::move(*graph));
		m_Ref = asset_at(file.string()).value_or(AssetRef{});
		m_Selection = GraphSelection{};
		m_Canvas.fit();
		m_CanvasLayer = -1;
		m_LastTabLayer = 0;
		m_ReapplyValues = true;
		m_SkeletonHandle = {};
		m_LoadedSkeleton = {};
		m_Message.clear();
		m_EditKey.clear();
		return true;
	}

	AssetRef GraphEditor::preview_skeleton() const
	{
		return m_Model ? m_Model->data().Skeleton : AssetRef{};
	}

	bool GraphEditor::save(std::string& error)
	{
		error.clear();
		if (!m_Model)
		{
			error = "there is nothing open";
			return false;
		}
		if (!graph_valid(m_Model->data(), error))
		{
			return false; // nothing is written: the file on disk stays a graph the importer takes
		}
		const std::string text = write_graph_file(m_Model->data());
		if (!write_file_atomic(m_File, text.data(), text.size(), error))
		{
			return false;
		}
		m_Model->mark_saved();
		return true;
	}

	void GraphEditor::clamp_selection()
	{
		if (!m_Model)
		{
			return;
		}
		const AnimationGraphData& graph = m_Model->data();
		if (graph.Layers.empty())
		{
			return;
		}
		m_Selection.Layer = std::clamp(m_Selection.Layer, 0, static_cast<int>(graph.Layers.size()) - 1);
		const GraphLayer& layer = graph.Layers[static_cast<size_t>(m_Selection.Layer)];
		if (m_Selection.State >= static_cast<int>(layer.States.size()))
		{
			m_Selection.State = -1;
		}
		if (m_Selection.Transition >= static_cast<int>(layer.Transitions.size()))
		{
			m_Selection.Transition = -1;
		}
	}

	std::vector<std::string> GraphEditor::parameter_names(bool floatsOnly) const
	{
		std::vector<std::string> names;
		for (const GraphParameter& parameter : m_Model->data().Parameters)
		{
			if (!floatsOnly || parameter.Type == GraphParamType::Float)
			{
				names.push_back(parameter.Name);
			}
		}
		return names;
	}

	std::vector<std::string> GraphEditor::state_names(const GraphLayer& layer) const
	{
		std::vector<std::string> names;
		for (const GraphState& state : layer.States)
		{
			names.push_back(state.Name);
		}
		return names;
	}

	bool GraphEditor::name_field(const std::string& key, const std::string& current, std::string& result, float width)
	{
		char local[sizeof(m_EditBuffer)];
		const bool mine = m_EditKey == key;
		char* buffer = mine ? m_EditBuffer : local;
		if (!mine)
		{
			std::snprintf(local, sizeof(local), "%s", current.c_str());
		}
		ImGui::SetNextItemWidth(width);
		ImGui::InputText(("##" + key).c_str(), buffer, GraphEditModel::MAX_NAME_LENGTH + 1, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		if (ImGui::IsItemActivated())
		{
			m_EditKey = key;
			std::snprintf(m_EditBuffer, sizeof(m_EditBuffer), "%s", current.c_str());
		}
		bool done = false;
		if (ImGui::IsItemDeactivated() && m_EditKey == key)
		{
			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				result = m_EditBuffer;
				done = true;
			}
			m_EditKey.clear();
		}
		return done;
	}

	void GraphEditor::draw_tools()
	{
		if (!m_Model)
		{
			return;
		}
		clamp_selection();
		// a drag of a slider changes the graph every frame: while an item is held with the mouse the changes make one undo step
		if (!m_ToolGesture && ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::GetActiveID() != 0 && !ImGui::GetIO().WantTextInput)
		{
			m_Model->begin_gesture();
			m_ToolGesture = true;
		}
		const AnimationGraphData& graph = m_Model->data();
		if (graph.Skeleton.valid() && !(graph.Skeleton == m_LoadedSkeleton))
		{
			m_LoadedSkeleton = graph.Skeleton;
			if (AssetManager* assets = Engine::get().get_module<AssetManager>())
			{
				m_SkeletonHandle = assets->load<SkeletonData>(graph.Skeleton);
			}
		}
		else if (!graph.Skeleton.valid())
		{
			m_LoadedSkeleton = {};
			m_SkeletonHandle = {};
		}

		ImGui::Text("%s", graph.Name.empty() ? m_File.stem().string().c_str() : graph.Name.c_str());
		ImGui::TextDisabled("%zu layers, %zu parameters", graph.Layers.size(), graph.Parameters.size());
		if (!m_Model->error().empty())
		{
			ImGui::TextColored(error_color(), "%s", m_Model->error().c_str());
		}
		if (!m_Message.empty())
		{
			ImGui::TextColored(error_color(), "%s", m_Message.c_str());
		}

		if (ImGui::CollapsingHeader("Preview parameters", ImGuiTreeNodeFlags_DefaultOpen))
		{
			draw_preview_controls();
		}
		if (ImGui::CollapsingHeader("Skeleton", ImGuiTreeNodeFlags_DefaultOpen))
		{
			draw_skeleton();
		}
		if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen))
		{
			draw_parameters();
		}
		if (ImGui::CollapsingHeader("Layers", ImGuiTreeNodeFlags_DefaultOpen))
		{
			draw_layers();
		}
		if (ImGui::CollapsingHeader("States", ImGuiTreeNodeFlags_DefaultOpen))
		{
			draw_states();
		}
		if (ImGui::CollapsingHeader("Transitions", ImGuiTreeNodeFlags_DefaultOpen))
		{
			draw_transitions();
		}
		if (m_ToolGesture && ImGui::GetActiveID() == 0)
		{
			m_Model->end_gesture();
			m_ToolGesture = false;
		}
	}

	// What the preview plays is the saved graph, so these controls are the parameters of that graph (the saved one may differ from the open one
	// until Save). The values are kept and put back when the graph is cooked again.
	void GraphEditor::draw_preview_controls()
	{
		ImGui::TextDisabled("The preview plays the saved graph: edits show after Save");
		AnimatorInstance* instance = m_Host.Preview.animator();
		const GraphParams* params = instance != nullptr ? instance->params() : nullptr;
		if (instance == nullptr || params == nullptr)
		{
			ImGui::TextDisabled("Not playing: the skeleton or the clips are loading, there is no skeleton, or the saved graph is not valid");
			m_ReapplyValues = true;
			return;
		}
		if (m_ReapplyValues)
		{
			for (const auto& [name, value] : m_PreviewValues)
			{
				instance->set_param(name, value);
			}
			m_ReapplyValues = false;
		}
		for (size_t i = 0; i < params->count(); i++)
		{
			const int index = static_cast<int>(i);
			const std::string name = params->name(index);
			ImGui::PushID(index);
			float value = params->get(index);
			switch (params->type_of(index))
			{
			case GraphParamType::Float:
				ImGui::SetNextItemWidth(160.0f);
				if (ImGui::DragFloat(name.c_str(), &value, 0.02f))
				{
					instance->set_param(name, value);
					m_PreviewValues[name] = value;
				}
				break;
			case GraphParamType::Int:
			{
				int whole = static_cast<int>(value);
				ImGui::SetNextItemWidth(160.0f);
				if (ImGui::DragInt(name.c_str(), &whole))
				{
					instance->set_param(name, static_cast<float>(whole));
					m_PreviewValues[name] = static_cast<float>(whole);
				}
				break;
			}
			case GraphParamType::Bool:
			{
				bool on = value != 0.0f;
				if (ImGui::Checkbox(name.c_str(), &on))
				{
					instance->set_param(name, on ? 1.0f : 0.0f);
					m_PreviewValues[name] = on ? 1.0f : 0.0f;
				}
				break;
			}
			case GraphParamType::Trigger:
				if (ImGui::Button(("Fire " + name).c_str()))
				{
					instance->trigger(name);
				}
				break;
			}
			ImGui::PopID();
		}
		for (size_t l = 0; l < instance->layer_count(); l++)
		{
			const std::string state = instance->state_name(l);
			const AnimationGraphData& graph = m_Model->data();
			ImGui::TextDisabled("%s: %s", l < graph.Layers.size() ? graph.Layers[l].Name.c_str() : "layer", state.c_str());
		}
	}

	void GraphEditor::draw_bottom(ImVec2)
	{
		if (!m_Model)
		{
			return;
		}
		clamp_selection();
		const AnimationGraphData& graph = m_Model->data();
		const bool forced = m_Selection.Layer != m_LastTabLayer; // the tool pane changed the layer: the tab follows
		if (ImGui::BeginTabBar("##graphlayers"))
		{
			for (size_t i = 0; i < graph.Layers.size(); i++)
			{
				const ImGuiTabItemFlags flags = forced && static_cast<int>(i) == m_Selection.Layer ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
				if (ImGui::BeginTabItem((graph.Layers[i].Name + "###layertab" + std::to_string(i)).c_str(), nullptr, flags))
				{
					if (!forced && m_Selection.Layer != static_cast<int>(i))
					{
						m_Selection = GraphSelection{ static_cast<int>(i), -1, -1 };
					}
					ImGui::EndTabItem();
				}
			}
			ImGui::EndTabBar();
		}
		m_LastTabLayer = m_Selection.Layer;
		if (m_CanvasLayer != m_Selection.Layer)
		{
			m_Canvas.fit();
			m_CanvasLayer = m_Selection.Layer;
		}
		std::string active;
		if (const AnimatorInstance* instance = m_Host.Preview.animator())
		{
			active = instance->state_name(static_cast<size_t>(m_Selection.Layer));
		}
		m_Canvas.draw(*m_Model, m_Selection, active);
	}

	void GraphEditor::draw_skeleton()
	{
		AssetFieldContext context;
		context.Database = &m_Host.Database;
		context.Expected = AssetType::Skeleton;
		std::string dropped;
		const AssetFieldEvent event = draw_asset_slot("##graphskeleton", "Skeleton", m_Model->data().Skeleton, context, dropped);
		if (event == AssetFieldEvent::Dropped)
		{
			AssetRef ref;
			m_Message.clear();
			if (dropped_asset(dropped, ".wskel", "a skeleton (.wskel)", ref, m_Message))
			{
				m_Model->set_skeleton(m_Host.Database.redirected(ref));
			}
		}
		else if (event == AssetFieldEvent::Cleared)
		{
			m_Model->set_skeleton(AssetRef{});
		}
		if (!m_Model->data().Skeleton.valid())
		{
			ImGui::TextDisabled("Drop a skeleton to preview the graph and to pick mask joints");
		}
	}

	void GraphEditor::draw_parameters()
	{
		const AnimationGraphData& graph = m_Model->data();
		for (size_t i = 0; i < graph.Parameters.size(); i++)
		{
			const GraphParameter parameter = graph.Parameters[i];
			ImGui::PushID(static_cast<int>(i));
			std::string renamed;
			if (name_field("param" + std::to_string(i), parameter.Name, renamed))
			{
				m_Model->rename_parameter(i, renamed);
			}
			ImGui::SameLine();
			ImGui::TextDisabled("%s", PARAM_TYPES[static_cast<int>(parameter.Type)]);
			ImGui::SameLine();
			float value = parameter.Default;
			ImGui::SetNextItemWidth(80.0f);
			switch (parameter.Type)
			{
			case GraphParamType::Float:
				if (ImGui::DragFloat("##default", &value, 0.01f)) { m_Model->set_parameter_default(i, value); }
				break;
			case GraphParamType::Int:
			{
				int whole = static_cast<int>(value);
				if (ImGui::DragInt("##default", &whole)) { m_Model->set_parameter_default(i, static_cast<float>(whole)); }
				break;
			}
			case GraphParamType::Bool:
			{
				bool on = value != 0.0f;
				if (ImGui::Checkbox("##default", &on)) { m_Model->set_parameter_default(i, on ? 1.0f : 0.0f); }
				break;
			}
			case GraphParamType::Trigger:
				ImGui::TextDisabled("-");
				break;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("X"))
			{
				m_Model->delete_parameter(i);
				ImGui::PopID();
				return; // the list changed under us
			}
			ImGui::PopID();
		}
		ImGui::SetNextItemWidth(120.0f);
		ImGui::InputText("##newparameter", m_NewParameter, sizeof(m_NewParameter));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		ImGui::Combo("##newparametertype", &m_NewParameterType, PARAM_TYPES, 4);
		ImGui::SameLine();
		if (ImGui::Button("Add parameter"))
		{
			if (m_Model->add_parameter(m_NewParameter, PARAM_TYPE_VALUES[m_NewParameterType], 0.0f))
			{
				m_NewParameter[0] = '\0';
			}
		}
	}

	void GraphEditor::draw_layers()
	{
		const AnimationGraphData& graph = m_Model->data();
		const size_t layerIndex = static_cast<size_t>(m_Selection.Layer);
		const GraphLayer layer = graph.Layers[layerIndex];

		ImGui::SetNextItemWidth(160.0f);
		if (ImGui::BeginCombo("Layer", layer.Name.c_str()))
		{
			for (size_t i = 0; i < graph.Layers.size(); i++)
			{
				if (ImGui::Selectable(graph.Layers[i].Name.c_str(), i == layerIndex))
				{
					m_Selection = GraphSelection{ static_cast<int>(i), -1, -1 };
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(120.0f);
		ImGui::InputText("##newlayer", m_NewLayer, sizeof(m_NewLayer));
		ImGui::SameLine();
		if (ImGui::Button("Add override layer") && m_Model->add_layer(m_NewLayer, GraphLayerMode::Override))
		{
			m_NewLayer[0] = '\0';
			m_Selection = GraphSelection{ static_cast<int>(m_Model->data().Layers.size()) - 1, -1, -1 };
			return;
		}
		ImGui::SameLine();
		if (ImGui::Button("Add additive layer") && m_Model->add_layer(m_NewLayer, GraphLayerMode::Additive))
		{
			m_NewLayer[0] = '\0';
			m_Selection = GraphSelection{ static_cast<int>(m_Model->data().Layers.size()) - 1, -1, -1 };
			return;
		}

		std::string renamed;
		if (name_field("layername" + std::to_string(layerIndex), layer.Name, renamed))
		{
			m_Model->rename_layer(layerIndex, renamed);
		}
		ImGui::SameLine();
		if (layerIndex == 0)
		{
			ImGui::TextDisabled("base layer");
		}
		else
		{
			int mode = layer.Mode == GraphLayerMode::Additive ? 1 : 0;
			ImGui::SetNextItemWidth(100.0f);
			if (ImGui::Combo("##layermode", &mode, "override\0additive\0"))
			{
				m_Model->set_layer_mode(layerIndex, mode == 1 ? GraphLayerMode::Additive : GraphLayerMode::Override);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Delete layer"))
			{
				m_Model->delete_layer(layerIndex);
				clamp_selection();
				return;
			}
		}

		if (layerIndex != 0)
		{
			float weight = layer.Weight;
			ImGui::SetNextItemWidth(100.0f);
			bool changed = ImGui::SliderFloat("Weight", &weight, 0.0f, 1.0f);
			std::string weightParameter = layer.WeightParameter;
			ImGui::SameLine();
			changed = name_combo("Driven by", weightParameter, parameter_names(true), "(constant)") || changed;
			if (changed)
			{
				m_Model->set_layer_weight(layerIndex, weight, weightParameter);
			}

			// the mask: a root joint (its subtree, weights dropping by the falloff per joint of depth) or a list of joints
			std::vector<std::string> joints;
			if (const SkeletonData* skeleton = m_SkeletonHandle.get())
			{
				joints = skeleton->JointNames;
			}
			std::string root = layer.MaskRoot;
			float falloff = layer.MaskFalloff;
			bool maskChanged = name_combo("Mask root", root, joints, "(whole body)", 160.0f);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(80.0f);
			maskChanged = ImGui::DragFloat("Falloff", &falloff, 0.01f, 0.0f, 1.0f) || maskChanged;
			std::vector<GraphMaskJoint> maskJoints = layer.MaskJoints;
			if (root.empty())
			{
				for (size_t j = 0; j < maskJoints.size(); j++)
				{
					ImGui::PushID(static_cast<int>(j));
					maskChanged = name_combo("##maskjoint", maskJoints[j].Joint, joints, nullptr, 150.0f) || maskChanged;
					ImGui::SameLine();
					ImGui::SetNextItemWidth(80.0f);
					maskChanged = ImGui::SliderFloat("##maskweight", &maskJoints[j].Weight, 0.0f, 1.0f) || maskChanged;
					ImGui::SameLine();
					if (ImGui::SmallButton("X"))
					{
						maskJoints.erase(maskJoints.begin() + static_cast<std::ptrdiff_t>(j));
						maskChanged = true;
						ImGui::PopID();
						break;
					}
					ImGui::PopID();
				}
				if (ImGui::SmallButton("Add mask joint") && !joints.empty())
				{
					maskJoints.push_back(GraphMaskJoint{ joints.front(), 1.0f });
					maskChanged = true;
				}
			}
			if (maskChanged)
			{
				m_Model->set_layer_mask(layerIndex, root, falloff, root.empty() ? maskJoints : std::vector<GraphMaskJoint>{});
			}
		}
	}

	void GraphEditor::draw_states()
	{
		const size_t layerIndex = static_cast<size_t>(m_Selection.Layer);
		const GraphLayer layer = m_Model->data().Layers[layerIndex];
		for (size_t i = 0; i < layer.States.size(); i++)
		{
			const GraphState& state = layer.States[i];
			const std::string label = state.Name + (state.Name == layer.DefaultState ? "  (default)" : "") + (state.Body == GraphBodyKind::Blend ? "  [blend]" : "");
			if (ImGui::Selectable((label + "##state" + std::to_string(i)).c_str(), m_Selection.State == static_cast<int>(i)))
			{
				m_Selection.State = static_cast<int>(i);
				m_Selection.Transition = -1;
			}
		}
		ImGui::SetNextItemWidth(140.0f);
		ImGui::InputText("##newstate", m_NewState, sizeof(m_NewState));
		ImGui::SameLine();
		if (ImGui::Button("Add state") && m_Model->add_state(layerIndex, m_NewState))
		{
			m_NewState[0] = '\0';
			m_Selection.State = static_cast<int>(m_Model->data().Layers[layerIndex].States.size()) - 1;
			m_Selection.Transition = -1;
			return;
		}
		if (m_Selection.State >= 0)
		{
			ImGui::Separator();
			draw_state_details(layer, static_cast<size_t>(m_Selection.State));
		}
	}

	void GraphEditor::draw_state_details(const GraphLayer& layer, size_t stateIndex)
	{
		const size_t layerIndex = static_cast<size_t>(m_Selection.Layer);
		const GraphState state = layer.States[stateIndex];
		ImGui::PushID("statedetails");

		std::string renamed;
		if (name_field("statename" + std::to_string(layerIndex) + "_" + std::to_string(stateIndex), state.Name, renamed))
		{
			m_Model->rename_state(layerIndex, stateIndex, renamed);
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Make default"))
		{
			m_Model->set_default_state(layerIndex, stateIndex);
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Delete state"))
		{
			m_Model->delete_state(layerIndex, stateIndex);
			clamp_selection();
			ImGui::PopID();
			return;
		}

		int body = state.Body == GraphBodyKind::Blend ? 1 : 0;
		if (ImGui::RadioButton("Clip", body == 0)) { m_Model->set_state_body(layerIndex, stateIndex, GraphBodyKind::Clip); }
		ImGui::SameLine();
		if (ImGui::RadioButton("Blend space", body == 1)) { m_Model->set_state_body(layerIndex, stateIndex, GraphBodyKind::Blend); }

		AssetFieldContext context;
		context.Database = &m_Host.Database;
		context.Expected = AssetType::AnimationClip;
		if (state.Body == GraphBodyKind::Clip)
		{
			std::string dropped;
			const AssetFieldEvent event = draw_asset_slot("##stateclip", "Clip", state.Clip, context, dropped);
			AssetRef ref;
			if (event == AssetFieldEvent::Dropped)
			{
				m_Message.clear();
				if (dropped_asset(dropped, ".wanim", "a clip (.wanim)", ref, m_Message))
				{
					m_Model->set_state_clip(layerIndex, stateIndex, ref);
				}
			}
			else if (event == AssetFieldEvent::Cleared)
			{
				m_Model->set_state_clip(layerIndex, stateIndex, AssetRef{});
			}
		}
		else
		{
			std::string x = state.BlendX;
			std::string y = state.BlendY;
			bool axesChanged = name_combo("X axis", x, parameter_names(true), nullptr);
			ImGui::SameLine();
			axesChanged = name_combo("Y axis", y, parameter_names(true), "(1D)") || axesChanged;
			if (axesChanged)
			{
				m_Model->set_blend_axes(layerIndex, stateIndex, x, y);
			}
			const bool twoD = !state.BlendY.empty();
			for (size_t s = 0; s < state.Samples.size(); s++)
			{
				const GraphBlendSample& sample = state.Samples[s];
				ImGui::PushID(static_cast<int>(s));
				std::string dropped;
				const AssetFieldEvent event = draw_asset_slot("##samplecip", nullptr, sample.Clip, context, dropped);
				AssetRef clip = sample.Clip;
				bool changed = false;
				if (event == AssetFieldEvent::Dropped)
				{
					m_Message.clear();
					changed = dropped_asset(dropped, ".wanim", "a clip (.wanim)", clip, m_Message);
				}
				else if (event == AssetFieldEvent::Cleared)
				{
					clip = AssetRef{};
					changed = true;
				}
				float px = sample.X;
				float py = sample.Y;
				ImGui::SetNextItemWidth(70.0f);
				changed = ImGui::DragFloat("##samplex", &px, 0.01f) || changed;
				if (twoD)
				{
					ImGui::SameLine();
					ImGui::SetNextItemWidth(70.0f);
					changed = ImGui::DragFloat("##sampley", &py, 0.01f) || changed;
				}
				ImGui::SameLine();
				const bool removed = ImGui::SmallButton("X");
				ImGui::PopID();
				if (removed)
				{
					m_Model->delete_sample(layerIndex, stateIndex, s);
					break;
				}
				if (changed)
				{
					m_Model->set_sample(layerIndex, stateIndex, s, clip, px, py);
				}
			}
			if (ImGui::SmallButton("Add sample"))
			{
				m_Model->add_sample(layerIndex, stateIndex, AssetRef{}, 0.0f, 0.0f);
			}
		}

		bool loop = state.Loop;
		float speed = state.Speed;
		std::string speedParameter = state.SpeedParameter;
		bool optionsChanged = ImGui::Checkbox("Loop", &loop);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		optionsChanged = ImGui::DragFloat("Speed", &speed, 0.01f, 0.0f, 10.0f) || optionsChanged;
		ImGui::SameLine();
		optionsChanged = name_combo("x parameter", speedParameter, parameter_names(true), "(none)") || optionsChanged;
		if (optionsChanged)
		{
			m_Model->set_state_options(layerIndex, stateIndex, loop, speed, speedParameter);
		}
		ImGui::PopID();
	}

	void GraphEditor::draw_transitions()
	{
		const size_t layerIndex = static_cast<size_t>(m_Selection.Layer);
		const GraphLayer layer = m_Model->data().Layers[layerIndex];
		for (size_t i = 0; i < layer.Transitions.size(); i++)
		{
			const GraphTransition& transition = layer.Transitions[i];
			const std::string label = std::to_string(i + 1) + ". " + (transition.From.empty() ? std::string("Any") : transition.From) + "  ->  " + transition.To;
			if (ImGui::Selectable((label + "##transition" + std::to_string(i)).c_str(), m_Selection.Transition == static_cast<int>(i)))
			{
				m_Selection.Transition = static_cast<int>(i);
				m_Selection.State = -1;
			}
		}
		if (ImGui::Button("Add transition"))
		{
			// from the selected state (or any), to the next state
			const std::vector<std::string> names = state_names(layer);
			const std::string from = m_Selection.State >= 0 ? layer.States[static_cast<size_t>(m_Selection.State)].Name : std::string{};
			std::string to = names.front();
			for (const std::string& name : names)
			{
				if (name != from)
				{
					to = name;
					break;
				}
			}
			if (m_Model->add_transition(layerIndex, from, to))
			{
				m_Selection.Transition = static_cast<int>(m_Model->data().Layers[layerIndex].Transitions.size()) - 1;
				m_Selection.State = -1;
			}
			return;
		}
		if (m_Selection.Transition >= 0)
		{
			ImGui::Separator();
			draw_transition_details(layer, static_cast<size_t>(m_Selection.Transition));
		}
	}

	void GraphEditor::draw_transition_details(const GraphLayer& layer, size_t transitionIndex)
	{
		const size_t layerIndex = static_cast<size_t>(m_Selection.Layer);
		const GraphTransition transition = layer.Transitions[transitionIndex];
		ImGui::PushID("transitiondetails");

		std::string from = transition.From;
		std::string to = transition.To;
		const std::vector<std::string> names = state_names(layer);
		bool endsChanged = name_combo("From", from, names, "Any");
		ImGui::SameLine();
		endsChanged = name_combo("To", to, names, nullptr) || endsChanged;
		if (endsChanged)
		{
			m_Model->set_transition_ends(layerIndex, transitionIndex, from, to);
		}

		if (ImGui::SmallButton("Up") && transitionIndex > 0)
		{
			m_Model->move_transition(layerIndex, transitionIndex, transitionIndex - 1);
			m_Selection.Transition--;
			ImGui::PopID();
			return;
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Down") && transitionIndex + 1 < layer.Transitions.size())
		{
			m_Model->move_transition(layerIndex, transitionIndex, transitionIndex + 1);
			m_Selection.Transition++;
			ImGui::PopID();
			return;
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Delete transition"))
		{
			m_Model->delete_transition(layerIndex, transitionIndex);
			clamp_selection();
			ImGui::PopID();
			return;
		}

		float duration = transition.Duration;
		bool hasExit = transition.ExitTime >= 0.0f;
		float exitTime = hasExit ? transition.ExitTime : 0.9f;
		ImGui::SetNextItemWidth(80.0f);
		bool optionsChanged = ImGui::DragFloat("Crossfade (s)", &duration, 0.01f, 0.0f, 10.0f);
		optionsChanged = ImGui::Checkbox("Exit time", &hasExit) || optionsChanged;
		if (hasExit)
		{
			ImGui::SameLine();
			ImGui::SetNextItemWidth(100.0f);
			optionsChanged = ImGui::SliderFloat("##exittime", &exitTime, 0.0f, 1.0f) || optionsChanged;
		}
		if (optionsChanged)
		{
			m_Model->set_transition_options(layerIndex, transitionIndex, duration, hasExit ? exitTime : -1.0f);
		}

		ImGui::TextUnformatted("Conditions (all must hold)");
		const std::vector<std::string> parameters = parameter_names(false);
		for (size_t c = 0; c < transition.Conditions.size(); c++)
		{
			GraphCondition condition = transition.Conditions[c];
			ImGui::PushID(static_cast<int>(c));
			std::string parameter = condition.Parameter;
			bool changed = name_combo("##conditionparameter", parameter, parameters, nullptr, 120.0f);
			const GraphParameter* known = find_parameter(m_Model->data(), parameter);
			const GraphParamType type = known != nullptr ? known->Type : GraphParamType::Float;
			if (changed)
			{
				condition.Parameter = parameter;
				const std::span<const OpName> allowed = ops_for(type);
				if (std::none_of(allowed.begin(), allowed.end(), [&](const OpName& op) { return op.Op == condition.Op; }))
				{
					condition.Op = allowed.front().Op; // the operator of another kind of parameter does not fit this one
				}
			}
			ImGui::SameLine();
			const std::span<const OpName> allowed = ops_for(type);
			const char* opLabel = "?";
			for (const OpName& op : allowed)
			{
				if (op.Op == condition.Op)
				{
					opLabel = op.Name;
				}
			}
			ImGui::SetNextItemWidth(80.0f);
			if (ImGui::BeginCombo("##conditionop", opLabel))
			{
				for (const OpName& op : allowed)
				{
					if (ImGui::Selectable(op.Name, op.Op == condition.Op))
					{
						condition.Op = op.Op;
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
			if (type == GraphParamType::Float || type == GraphParamType::Int)
			{
				ImGui::SameLine();
				ImGui::SetNextItemWidth(70.0f);
				changed = ImGui::DragFloat("##conditionvalue", &condition.Value, 0.01f) || changed;
			}
			ImGui::SameLine();
			const bool removed = ImGui::SmallButton("X");
			ImGui::PopID();
			if (removed)
			{
				m_Model->delete_condition(layerIndex, transitionIndex, c);
				break;
			}
			if (changed)
			{
				m_Model->set_condition(layerIndex, transitionIndex, c, condition);
			}
		}
		if (ImGui::SmallButton("Add condition"))
		{
			if (m_Model->data().Parameters.empty())
			{
				m_Message = "Add a parameter first";
			}
			else
			{
				m_Message.clear();
				const GraphParameter& first = m_Model->data().Parameters.front();
				m_Model->add_condition(layerIndex, transitionIndex, GraphCondition{ first.Name, ops_for(first.Type).front().Op, 0.0f });
			}
		}
		ImGui::PopID();
	}
}
