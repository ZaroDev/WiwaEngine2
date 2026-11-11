#include <editor_common.h>
#include "bt_editor.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <imgui.h>

#include <ai/bt_lua_host.h>
#include <assets/asset_manager.h>
#include <core/engine.h>
#include <scripting/script_module.h>

namespace wiwa
{
	namespace
	{
		using Json = nlohmann::json;

		const char* const OPS[] = { "==", "!=", "<", "<=", ">", ">=", "true", "false" };
		const char* const TYPES[] = { "bool", "int", "float", "vec3", "string", "entity" };

		const BtParamSpec* spec_of(const BtNodeSpec& spec, const char* name)
		{
			for (const BtParamSpec& param : spec.Params)
			{
				if (param.Name == name)
				{
					return &param;
				}
			}
			return nullptr;
		}
	}

	bool BehaviorTreeEditor::load(const std::filesystem::path& file, std::string& error)
	{
		std::ifstream stream(file, std::ios::binary);
		if (!stream)
		{
			error = "cannot read the file";
			return false;
		}
		std::stringstream text;
		text << stream.rdbuf();
		std::optional<BehaviorTreeDoc> doc = read_bt_file(text.str(), error);
		if (!doc)
		{
			return false;
		}
		m_File = file;
		m_Model = std::make_unique<BtEditModel>(std::move(*doc));
		m_Selected.clear();
		m_FitNext = true;
		m_Message.clear();
		m_ScriptsLoaded = false;
		return true;
	}

	bool BehaviorTreeEditor::save(std::string& error)
	{
		if (!m_Model)
		{
			error = "nothing to save";
			return false;
		}
		if (!m_Model->save(m_File))
		{
			error = m_Model->error();
			return false;
		}
		return true;
	}

	// The scripts of the catalog that are tree leaves, with their properties, for the palette and the node inspector.
	void BehaviorTreeEditor::refresh_scripts()
	{
		m_ScriptsLoaded = true;
		m_Leaves.clear();
		m_Scripts.clear();
		ScriptingModule* scripting = Engine::get().get_module<ScriptingModule>();
		AssetManager* assets = Engine::get().get_module<AssetManager>();
		ScriptRuntime* runtime = scripting != nullptr ? scripting->get_runtime() : nullptr;
		if (runtime == nullptr || assets == nullptr)
		{
			return;
		}
		for (const AssetCatalog::Entry& entry : assets->catalog_entries())
		{
			if (!entry.Path.ends_with(".lua"))
			{
				continue;
			}
			std::string error;
			ScriptClass* scriptClass = runtime->load(AssetRef{ entry.Guid, 0 }, error);
			if (scriptClass == nullptr)
			{
				continue;
			}
			const std::string kind = bt_leaf_kind(scriptClass->Class);
			if (kind.empty())
			{
				continue;
			}
			LeafInfo info;
			info.Script = BtLeafScript{ scriptClass->Name, entry.Guid.to_string(), kind };
			info.Properties = scriptClass->Properties;
			m_Scripts.push_back(info.Script);
			m_Leaves.push_back(std::move(info));
		}
		std::sort(m_Scripts.begin(), m_Scripts.end(), [](const BtLeafScript& a, const BtLeafScript& b) { return a.Name < b.Name; });
	}

	void BehaviorTreeEditor::draw_preview(ImVec2 size)
	{
		m_Frames++;
		if (!m_Model)
		{
			return;
		}
		if (!m_ScriptsLoaded)
		{
			refresh_scripts();
		}
		if (m_FitNext)
		{
			m_Canvas.fit();
			m_FitNext = false;
		}
		ImGui::BeginChild("##btcanvas", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
		m_Canvas.draw(*m_Model, m_Selected, m_Scripts);
		ImGui::EndChild();
	}

	bool BehaviorTreeEditor::text_field(const char* id, const std::string& current, std::string& result, float width)
	{
		const std::string key = id;
		if (m_EditKey != key)
		{
			std::snprintf(m_EditBuffer, sizeof(m_EditBuffer), "%s", current.c_str());
		}
		ImGui::SetNextItemWidth(width);
		ImGui::InputText(id, m_EditBuffer, sizeof(m_EditBuffer));
		if (ImGui::IsItemActivated())
		{
			m_EditKey = key;
		}
		const bool done = ImGui::IsItemDeactivatedAfterEdit();
		if (ImGui::IsItemDeactivated() && m_EditKey == key)
		{
			m_EditKey.clear();
		}
		if (done)
		{
			result = m_EditBuffer;
		}
		return done;
	}

	void BehaviorTreeEditor::gesture_of_last_item()
	{
		if (ImGui::IsItemActivated() && !m_ToolGesture)
		{
			m_Model->begin_gesture();
			m_ToolGesture = true;
		}
		if (ImGui::IsItemDeactivated() && m_ToolGesture)
		{
			m_Model->end_gesture();
			m_ToolGesture = false;
		}
	}

	bool BehaviorTreeEditor::number_field(const std::string& node, const char* param, double current, float minimum, float maximum, float speed)
	{
		float value = static_cast<float>(current);
		const std::string id = std::string("##") + param + node;
		ImGui::SetNextItemWidth(110.0f);
		const bool changed = ImGui::DragFloat(id.c_str(), &value, speed, minimum, maximum, "%.2f");
		if (ImGui::IsItemActivated() && !m_ToolGesture)
		{
			m_Model->begin_gesture();
			m_ToolGesture = true;
		}
		if (changed)
		{
			m_Model->set_param(node, param, std::max(value, minimum));
		}
		if (ImGui::IsItemDeactivated() && m_ToolGesture)
		{
			m_Model->end_gesture();
			m_ToolGesture = false;
		}
		return changed;
	}

	std::vector<std::string> BehaviorTreeEditor::keys_of(bool vectorOrEntityOnly) const
	{
		std::vector<std::string> keys;
		for (const BtKeyDesc& key : m_Model->doc().Blackboard)
		{
			if (!vectorOrEntityOnly || key.Type == BtValueType::Vec3 || key.Type == BtValueType::Entity)
			{
				keys.push_back(key.Key);
			}
		}
		return keys;
	}

	void BehaviorTreeEditor::draw_tools()
	{
		if (!m_Model)
		{
			ImGui::TextDisabled("No tree");
			return;
		}
		if (!m_ScriptsLoaded)
		{
			refresh_scripts();
		}

		if (ImGui::Button("Auto layout"))
		{
			BtCanvas::auto_layout(*m_Model);
			m_FitNext = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Fit"))
		{
			m_FitNext = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Refresh scripts"))
		{
			refresh_scripts();
		}
		ImGui::SameLine();
		ImGui::TextDisabled("%zu nodes", m_Model->doc().Nodes.size());
		if (!m_Model->error().empty())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "%s", m_Model->error().c_str());
		}
		if (m_Model->doc().Nodes.empty())
		{
			ImGui::TextWrapped("Right click the canvas to add the root node.");
		}

		if (ImGui::CollapsingHeader("Node", ImGuiTreeNodeFlags_DefaultOpen))
		{
			draw_node();
		}
		if (ImGui::CollapsingHeader("Blackboard", ImGuiTreeNodeFlags_DefaultOpen))
		{
			draw_blackboard();
		}
		if (ImGui::CollapsingHeader("Problems", ImGuiTreeNodeFlags_DefaultOpen))
		{
			draw_problems();
		}
	}

	void BehaviorTreeEditor::draw_node()
	{
		const BtNodeDesc* node = m_Selected.empty() ? nullptr : m_Model->doc().find(m_Selected);
		if (node == nullptr)
		{
			ImGui::TextDisabled("Select a node on the canvas.");
			return;
		}
		const std::string id = node->Id;
		ImGui::Text("%s  (%s)", node->Type.c_str(), id.c_str());
		std::string name;
		if (text_field(("Name##name" + id).c_str(), node->Name, name))
		{
			m_Model->rename_node(id, name);
			node = m_Model->doc().find(id);
		}
		if (node != nullptr)
		{
			draw_params(*node);
		}
	}

	void BehaviorTreeEditor::draw_params(const BtNodeDesc& node)
	{
		const BtNodeSpec* spec = bt_find_node_spec(node.Type);
		if (spec == nullptr)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), "Unknown node type");
			return;
		}
		const std::string id = node.Id;
		for (const BtParamSpec& param : spec->Params)
		{
			const std::string label = param.Name + (param.Required ? " *" : "");
			ImGui::PushID(param.Name.c_str());
			const auto found = node.Params.find(param.Name);
			const Json current = found != node.Params.end() ? *found : Json();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(label.c_str());
			ImGui::SameLine(120.0f);
			switch (param.Kind)
			{
			case BtParamKind::Bool:
			{
				bool value = current.is_boolean() ? current.get<bool>() : false;
				if (ImGui::Checkbox("##v", &value))
				{
					m_Model->set_param(id, param.Name, value);
				}
				break;
			}
			case BtParamKind::Number:
				number_field(id, param.Name.c_str(), current.is_number() ? current.get<double>() : 0.0, 0.0f, 100000.0f);
				if (!param.Required && found != node.Params.end())
				{
					ImGui::SameLine();
					if (ImGui::SmallButton("x"))
					{
						m_Model->set_param(id, param.Name, Json());
					}
				}
				break;
			case BtParamKind::String:
			{
				std::string text;
				if (text_field("##v", current.is_string() ? current.get<std::string>() : std::string(), text))
				{
					m_Model->set_param(id, param.Name, text);
				}
				break;
			}
			case BtParamKind::Op:
			case BtParamKind::Policy:
			{
				const bool policy = param.Kind == BtParamKind::Policy;
				const char* const policies[] = { "all", "one" };
				const std::string shown = current.is_string() ? current.get<std::string>() : std::string(policy ? "(default)" : "(choose)");
				ImGui::SetNextItemWidth(110.0f);
				if (ImGui::BeginCombo("##v", shown.c_str()))
				{
					const size_t count = policy ? 2 : std::size(OPS);
					for (size_t i = 0; i < count; i++)
					{
						const char* option = policy ? policies[i] : OPS[i];
						if (ImGui::Selectable(option, shown == option))
						{
							m_Model->set_param(id, param.Name, option);
						}
					}
					ImGui::EndCombo();
				}
				break;
			}
			case BtParamKind::Key:
			{
				const std::string shown = current.is_string() ? current.get<std::string>() : std::string("(choose)");
				ImGui::SetNextItemWidth(150.0f);
				if (ImGui::BeginCombo("##v", shown.c_str()))
				{
					for (const std::string& key : keys_of(false))
					{
						if (ImGui::Selectable(key.c_str(), key == shown))
						{
							m_Model->set_param(id, param.Name, key);
						}
					}
					ImGui::EndCombo();
				}
				break;
			}
			case BtParamKind::Value:
			{
				const auto keyParam = node.Params.find(param.KeyParam);
				const BtKeyDesc* key = keyParam != node.Params.end() && keyParam->is_string() ? m_Model->doc().find_key(keyParam->get<std::string>()) : nullptr;
				if (key == nullptr)
				{
					ImGui::TextDisabled("choose the key first");
					break;
				}
				switch (key->Type)
				{
				case BtValueType::Bool:
				{
					bool value = current.is_boolean() ? current.get<bool>() : false;
					if (ImGui::Checkbox("##v", &value))
					{
						m_Model->set_param(id, param.Name, value);
					}
					break;
				}
				case BtValueType::Int:
				case BtValueType::Float:
				{
					float value = current.is_number() ? current.get<float>() : 0.0f;
					ImGui::SetNextItemWidth(110.0f);
					const bool changed = key->Type == BtValueType::Int ? ImGui::DragFloat("##v", &value, 1.0f, -1.0e6f, 1.0e6f, "%.0f") : ImGui::DragFloat("##v", &value, 0.05f);
					gesture_of_last_item();
					if (changed)
					{
						m_Model->set_param(id, param.Name, key->Type == BtValueType::Int ? Json(static_cast<i64>(std::lround(value))) : Json(value));
					}
					break;
				}
				case BtValueType::Vec3:
				{
					float v[3] = { 0.0f, 0.0f, 0.0f };
					if (bt_json_fits(BtValueType::Vec3, current)) { v[0] = current[0].get<float>(); v[1] = current[1].get<float>(); v[2] = current[2].get<float>(); }
					ImGui::SetNextItemWidth(220.0f);
					const bool changed = ImGui::DragFloat3("##v", v, 0.05f);
					gesture_of_last_item();
					if (changed)
					{
						m_Model->set_param(id, param.Name, Json::array({ v[0], v[1], v[2] }));
					}
					break;
				}
				case BtValueType::String:
				{
					std::string text;
					if (text_field("##v", current.is_string() ? current.get<std::string>() : std::string(), text))
					{
						m_Model->set_param(id, param.Name, text);
					}
					break;
				}
				case BtValueType::Entity:
					ImGui::TextDisabled("none (an entity is chosen at run time)");
					break;
				}
				break;
			}
			case BtParamKind::Target:
			{
				const bool isKey = current.is_string() && current.get<std::string>().rfind("key:", 0) == 0;
				int mode = isKey ? 1 : 0;
				ImGui::SetNextItemWidth(80.0f);
				if (ImGui::RadioButton("point", mode == 0) && isKey)
				{
					m_Model->set_param(id, param.Name, Json::array({ 0, 0, 0 }));
				}
				ImGui::SameLine();
				if (ImGui::RadioButton("key", mode == 1) && !isKey)
				{
					const std::vector<std::string> keys = keys_of(true);
					m_Model->set_param(id, param.Name, keys.empty() ? Json("key:") : Json("key:" + keys.front()));
				}
				ImGui::SameLine();
				if (isKey)
				{
					const std::string shown = current.get<std::string>().substr(4);
					ImGui::SetNextItemWidth(130.0f);
					if (ImGui::BeginCombo("##target", shown.c_str()))
					{
						for (const std::string& key : keys_of(true))
						{
							if (ImGui::Selectable(key.c_str(), key == shown))
							{
								m_Model->set_param(id, param.Name, "key:" + key);
							}
						}
						ImGui::EndCombo();
					}
				}
				else
				{
					float v[3] = { 0.0f, 0.0f, 0.0f };
					if (bt_json_fits(BtValueType::Vec3, current)) { v[0] = current[0].get<float>(); v[1] = current[1].get<float>(); v[2] = current[2].get<float>(); }
					ImGui::SetNextItemWidth(200.0f);
					const bool changed = ImGui::DragFloat3("##point", v, 0.05f);
					gesture_of_last_item();
					if (changed)
					{
						m_Model->set_param(id, param.Name, Json::array({ v[0], v[1], v[2] }));
					}
				}
				break;
			}
			case BtParamKind::Script:
			{
				const std::string guid = current.is_string() ? current.get<std::string>() : std::string();
				std::string shown = guid.empty() ? "(choose a script)" : guid.substr(0, 8);
				for (const BtLeafScript& script : m_Scripts)
				{
					if (script.Guid == guid)
					{
						shown = script.Name;
					}
				}
				ImGui::SetNextItemWidth(200.0f);
				if (ImGui::BeginCombo("##v", shown.c_str()))
				{
					const std::string wanted = node.Type == "LuaCondition" ? "condition" : "action";
					for (const BtLeafScript& script : m_Scripts)
					{
						if (script.Kind == wanted && ImGui::Selectable(script.Name.c_str(), script.Guid == guid))
						{
							m_Model->set_param(id, param.Name, script.Guid);
						}
					}
					ImGui::EndCombo();
				}
				break;
			}
			}
			ImGui::PopID();
		}

		if (node.Type == "LuaAction" || node.Type == "LuaCondition")
		{
			draw_script_properties(node);
		}
	}

	// The properties the script of a Lua leaf declares, as parameters of the node (the defaults show until they are set).
	void BehaviorTreeEditor::draw_script_properties(const BtNodeDesc& node)
	{
		const auto script = node.Params.find("script");
		if (script == node.Params.end() || !script->is_string())
		{
			return;
		}
		const LeafInfo* info = nullptr;
		for (const LeafInfo& candidate : m_Leaves)
		{
			if (candidate.Script.Guid == script->get<std::string>())
			{
				info = &candidate;
			}
		}
		if (info == nullptr)
		{
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "The script is not among the tree leaves of the project.");
			return;
		}
		const std::string id = node.Id;
		for (const FieldDescriptor& property : info->Properties)
		{
			ImGui::PushID(property.Name.c_str());
			const auto found = node.Params.find(property.Name);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(property.Name.c_str());
			ImGui::SameLine(120.0f);
			switch (property.Type)
			{
			case FieldType::Bool:
			{
				bool value = found != node.Params.end() && found->is_boolean() ? found->get<bool>() : std::get<bool>(property.Default);
				if (ImGui::Checkbox("##v", &value))
				{
					m_Model->set_param(id, property.Name, value);
				}
				break;
			}
			case FieldType::Int:
			case FieldType::Float:
			{
				const bool integer = property.Type == FieldType::Int;
				float value = found != node.Params.end() && found->is_number() ? found->get<float>() : (integer ? static_cast<float>(std::get<i32>(property.Default)) : std::get<float>(property.Default));
				ImGui::SetNextItemWidth(110.0f);
				const bool changed = ImGui::DragFloat("##v", &value, integer ? 1.0f : 0.05f, -1.0e6f, 1.0e6f, integer ? "%.0f" : "%.2f");
				gesture_of_last_item();
				if (changed)
				{
					m_Model->set_param(id, property.Name, integer ? Json(static_cast<i64>(std::lround(value))) : Json(value));
				}
				break;
			}
			case FieldType::String:
			{
				std::string text;
				if (text_field("##v", found != node.Params.end() && found->is_string() ? found->get<std::string>() : std::get<std::string>(property.Default), text))
				{
					m_Model->set_param(id, property.Name, text);
				}
				break;
			}
			case FieldType::Vec3:
			{
				const glm::vec3 fallback = std::get<glm::vec3>(property.Default);
				float v[3] = { fallback.x, fallback.y, fallback.z };
				if (found != node.Params.end() && bt_json_fits(BtValueType::Vec3, *found)) { v[0] = (*found)[0].get<float>(); v[1] = (*found)[1].get<float>(); v[2] = (*found)[2].get<float>(); }
				ImGui::SetNextItemWidth(220.0f);
				const bool changed = ImGui::DragFloat3("##v", v, 0.05f);
				gesture_of_last_item();
				if (changed)
				{
					m_Model->set_param(id, property.Name, Json::array({ v[0], v[1], v[2] }));
				}
				break;
			}
			default:
				ImGui::TextDisabled("(not editable here)");
				break;
			}
			ImGui::PopID();
		}
	}

	void BehaviorTreeEditor::draw_blackboard()
	{
		const std::vector<BtKeyDesc> keys = m_Model->doc().Blackboard; // a copy: the model changes while the rows are drawn
		for (size_t i = 0; i < keys.size(); i++)
		{
			const BtKeyDesc& key = keys[i];
			ImGui::PushID(static_cast<int>(i));
			std::string renamed;
			if (text_field("##name", key.Key, renamed, 110.0f))
			{
				m_Model->rename_key(key.Key, renamed);
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(80.0f);
			if (ImGui::BeginCombo("##type", bt_value_type_name(key.Type)))
			{
				for (int t = 0; t < 6; t++)
				{
					if (ImGui::Selectable(TYPES[t], static_cast<int>(key.Type) == t) && static_cast<int>(key.Type) != t)
					{
						if (!m_Model->retype_key(key.Key, static_cast<BtValueType>(t)) && !m_Model->users_of_key(key.Key).empty())
						{
							m_ForceKey = key.Key;
							m_ForceType = t;
						}
					}
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			switch (key.Type)
			{
			case BtValueType::Bool:
			{
				bool value = key.Default.is_boolean() ? key.Default.get<bool>() : false;
				if (ImGui::Checkbox("##default", &value))
				{
					m_Model->set_key_default(key.Key, value);
				}
				break;
			}
			case BtValueType::Int:
			case BtValueType::Float:
			{
				float value = key.Default.is_number() ? key.Default.get<float>() : 0.0f;
				ImGui::SetNextItemWidth(80.0f);
				const bool changed = ImGui::DragFloat("##default", &value, key.Type == BtValueType::Int ? 1.0f : 0.05f, -1.0e6f, 1.0e6f, key.Type == BtValueType::Int ? "%.0f" : "%.2f");
				gesture_of_last_item();
				if (changed)
				{
					m_Model->set_key_default(key.Key, key.Type == BtValueType::Int ? Json(static_cast<i64>(std::lround(value))) : Json(value));
				}
				break;
			}
			case BtValueType::Vec3:
			{
				float v[3] = { 0.0f, 0.0f, 0.0f };
				if (bt_json_fits(BtValueType::Vec3, key.Default)) { v[0] = key.Default[0].get<float>(); v[1] = key.Default[1].get<float>(); v[2] = key.Default[2].get<float>(); }
				ImGui::SetNextItemWidth(200.0f);
				const bool changed = ImGui::DragFloat3("##default", v, 0.05f);
				gesture_of_last_item();
				if (changed)
				{
					m_Model->set_key_default(key.Key, Json::array({ v[0], v[1], v[2] }));
				}
				break;
			}
			case BtValueType::String:
			{
				std::string text;
				if (text_field("##default", key.Default.is_string() ? key.Default.get<std::string>() : std::string(), text, 110.0f))
				{
					m_Model->set_key_default(key.Key, text);
				}
				break;
			}
			case BtValueType::Entity:
				ImGui::TextDisabled("none");
				break;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Remove"))
			{
				if (!m_Model->remove_key(key.Key) && !m_Model->users_of_key(key.Key).empty())
				{
					m_ForceKey = key.Key;
					m_ForceType = -1;
				}
			}
			ImGui::PopID();
		}

		if (!m_ForceKey.empty())
		{
			ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "%s", m_Model->error().c_str());
			if (ImGui::Button("Do it anyway (clears those parameters)"))
			{
				if (m_ForceType < 0)
				{
					m_Model->remove_key(m_ForceKey, true);
				}
				else
				{
					m_Model->retype_key(m_ForceKey, static_cast<BtValueType>(m_ForceType), true);
				}
				m_ForceKey.clear();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				m_ForceKey.clear();
				m_Model->clear_error();
			}
		}

		ImGui::SetNextItemWidth(110.0f);
		ImGui::InputText("##newkey", m_NewKey, sizeof(m_NewKey));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(80.0f);
		ImGui::Combo("##newtype", &m_NewKeyType, "bool\0int\0float\0vec3\0string\0entity\0");
		ImGui::SameLine();
		if (ImGui::Button("Add key") && m_NewKey[0] != '\0')
		{
			if (m_Model->add_key(m_NewKey, static_cast<BtValueType>(m_NewKeyType)))
			{
				m_NewKey[0] = '\0';
			}
		}
	}

	void BehaviorTreeEditor::draw_problems()
	{
		const std::vector<BtProblem> problems = m_Model->problems();
		if (problems.empty())
		{
			ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "No problems");
			return;
		}
		for (size_t i = 0; i < problems.size(); i++)
		{
			const BtProblem& problem = problems[i];
			ImGui::PushID(static_cast<int>(i));
			const std::string label = (problem.Error ? "Error: " : "Warning: ") + (problem.NodeId.empty() ? std::string() : problem.NodeId + ": ") + problem.Message;
			ImGui::PushStyleColor(ImGuiCol_Text, problem.Error ? ImVec4(1.0f, 0.5f, 0.4f, 1.0f) : ImVec4(1.0f, 0.85f, 0.4f, 1.0f));
			if (ImGui::Selectable(label.c_str(), !problem.NodeId.empty() && problem.NodeId == m_Selected) && !problem.NodeId.empty())
			{
				m_Selected = problem.NodeId;
			}
			ImGui::PopStyleColor();
			ImGui::PopID();
		}
	}
}
