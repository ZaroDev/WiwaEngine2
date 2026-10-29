#include <editor_common.h>
#include "inspector_panel.h"

#include <imgui.h>

#include <scene/reflection/component_registry.h>

namespace
{
	const ImVec4 OVERRIDDEN_COLOR(0.45f, 0.70f, 1.00f, 1.0f);
	const ImVec4 MISSING_COLOR(1.00f, 0.40f, 0.40f, 1.0f);

	bool is_mandatory(const std::string& component)
	{
		return component == "Name" || component == "Transform";
	}

	std::string describe_ref(const wiwa::AssetRef& ref)
	{
		if (!ref.valid())
		{
			return "(none)";
		}
		return ref.Guid.to_string().substr(0, 8) + (ref.LocalId != 0 ? ":" + std::to_string(ref.LocalId) : std::string());
	}
}

void InspectorPanel::draw()
{
	ImGui::SetNextWindowPos(ImVec2(380.0f, 12.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(380.0f, 520.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Inspector") && m_Editor.get_scene() != nullptr)
	{
		const entt::entity entity = m_Editor.selected();
		if (entity == entt::null || !m_Editor.scene().registry().valid(entity))
		{
			ImGui::TextDisabled("Nothing selected");
		}
		else
		{
			switch (m_Editor.kind_of(entity))
			{
			case wiwa::SceneEditor::Kind::PrefabNode:
				ImGui::PushStyleColor(ImGuiCol_Text, OVERRIDDEN_COLOR); ImGui::TextWrapped("Part of a prefab instance: edits are saved as overrides of the instance"); ImGui::PopStyleColor();
				break;
			case wiwa::SceneEditor::Kind::AddedChild:
				ImGui::TextColored(ImVec4(0.6f, 1.0f, 0.65f, 1.0f), "Added to a prefab instance: saved as an override of the instance");
				break;
			case wiwa::SceneEditor::Kind::Authored:
				break;
			}
			ImGui::Separator();

			for (const wiwa::ComponentInfo* info : wiwa::ComponentRegistry::get().all())
			{
				if (info->Name != "Id" && info->Has(m_Editor.scene().registry(), entity))
				{
					draw_component(entity, *info);
				}
			}

			ImGui::Separator();
			if (ImGui::Button("Add component"))
			{
				ImGui::OpenPopup("add_component");
			}
			if (ImGui::BeginPopup("add_component"))
			{
				for (const wiwa::ComponentInfo* info : wiwa::ComponentRegistry::get().all())
				{
					if (info->Name == "Id" || info->Name == "PrefabInstance" || info->Has(m_Editor.scene().registry(), entity))
					{
						continue;
					}
					if (ImGui::MenuItem(info->Name.c_str()))
					{
						const std::string name = info->Name;
						m_Deferred = [this, entity, name]() { m_Editor.add_component(entity, name); };
					}
				}
				ImGui::EndPopup();
			}
		}
	}
	ImGui::End();

	if (m_Deferred)
	{
		std::function<void()> action = std::move(m_Deferred);
		m_Deferred = nullptr;
		action();
	}
}

void InspectorPanel::draw_component(entt::entity entity, const wiwa::ComponentInfo& info)
{
	ImGui::PushID(info.Name.c_str());
	const bool open = ImGui::CollapsingHeader(info.Name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

	if (!is_mandatory(info.Name) && info.Name != "PrefabInstance" && ImGui::BeginPopupContextItem())
	{
		if (ImGui::MenuItem("Remove component"))
		{
			const std::string name = info.Name;
			m_Deferred = [this, entity, name]() { m_Editor.remove_component(entity, name); };
		}
		ImGui::EndPopup();
	}

	if (open)
	{
		if (info.Name == "PrefabInstance")
		{
			// the link to the prefab is not edited here
			const wiwa::PrefabInstance& instance = m_Editor.scene().registry().get<wiwa::PrefabInstance>(entity);
			ImGui::Text("Prefab: %s", describe_ref(instance.Prefab).c_str());
			ImGui::Text("Overrides: %zu", instance.Overrides.size());
			if (ImGui::Button("Unpack"))
			{
				m_Deferred = [this, entity]() { m_Editor.unpack(entity); };
			}
			ImGui::SameLine();
			if (ImGui::Button("Revert all overrides"))
			{
				m_Deferred = [this, entity]() { m_Editor.revert_instance(entity); };
			}
		}
		else
		{
			for (size_t i = 0; i < info.Fields.size(); i++)
			{
				ImGui::PushID(static_cast<int>(i));
				const wiwa::FieldValue current = info.Get(m_Editor.scene().registry(), entity, i);
				wiwa::FieldValue edited = current;
				if (draw_field(entity, info, i, current, edited))
				{
					m_Editor.set_field(entity, info.Name, info.Fields[i].Name, edited);
				}
				ImGui::PopID();
			}
		}
	}
	ImGui::PopID();
}

bool InspectorPanel::draw_field(entt::entity entity, const wiwa::ComponentInfo& info, size_t index, const wiwa::FieldValue& current, wiwa::FieldValue& value)
{
	using namespace wiwa;
	const FieldDescriptor& field = info.Fields[index];
	const char* label = field.Name.c_str();
	const bool overridden = m_Editor.is_overridden(entity, info.Name, field.Name);
	bool changed = false;

	if (overridden)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, OVERRIDDEN_COLOR);
	}

	switch (field.Type)
	{
	case FieldType::Bool:
	{
		bool v = std::get<bool>(current);
		if (ImGui::Checkbox(label, &v)) { value = v; changed = true; }
		break;
	}
	case FieldType::Int:
	{
		int v = std::get<i32>(current);
		if (ImGui::DragInt(label, &v)) { value = static_cast<i32>(v); changed = true; }
		break;
	}
	case FieldType::Enum:
	{
		int v = std::get<i32>(current);
		std::vector<const char*> names;
		for (const std::string& name : field.Meta.EnumNames)
		{
			names.push_back(name.c_str());
		}
		if (!names.empty() && ImGui::Combo(label, &v, names.data(), static_cast<int>(names.size()))) { value = static_cast<i32>(v); changed = true; }
		break;
	}
	case FieldType::Float:
	{
		float v = std::get<float>(current);
		const bool edited = field.Meta.HasRange ? ImGui::SliderFloat(label, &v, field.Meta.Min, field.Meta.Max) : ImGui::DragFloat(label, &v, 0.05f);
		if (edited) { value = v; changed = true; }
		break;
	}
	case FieldType::Vec2:
	{
		glm::vec2 v = std::get<glm::vec2>(current);
		if (ImGui::DragFloat2(label, &v.x, 0.05f)) { value = v; changed = true; }
		break;
	}
	case FieldType::Vec3:
	{
		glm::vec3 v = std::get<glm::vec3>(current);
		if (ImGui::DragFloat3(label, &v.x, 0.05f)) { value = v; changed = true; }
		break;
	}
	case FieldType::Vec4:
	{
		glm::vec4 v = std::get<glm::vec4>(current);
		if (ImGui::DragFloat4(label, &v.x, 0.05f)) { value = v; changed = true; }
		break;
	}
	case FieldType::Color:
	{
		glm::vec4 v = std::get<glm::vec4>(current);
		if (ImGui::ColorEdit4(label, &v.x)) { value = v; changed = true; }
		break;
	}
	case FieldType::Quat:
	{
		const glm::quat q = std::get<glm::quat>(current);
		const std::string key = std::to_string(entt::to_integral(entity)) + info.Name + field.Name;
		if (key != m_EulerKey || glm::length(glm::vec4(q.x, q.y, q.z, q.w) - glm::vec4(m_EulerQuat.x, m_EulerQuat.y, m_EulerQuat.z, m_EulerQuat.w)) > 1e-4f)
		{
			m_EulerKey = key;
			m_Euler = glm::degrees(glm::eulerAngles(q));
			m_EulerQuat = q;
		}
		if (ImGui::DragFloat3(label, &m_Euler.x, 0.5f))
		{
			m_EulerQuat = glm::quat(glm::radians(m_Euler));
			value = m_EulerQuat;
			changed = true;
		}
		break;
	}
	case FieldType::String:
	{
		char buffer[256];
		const std::string& text = std::get<std::string>(current);
		std::snprintf(buffer, sizeof(buffer), "%s", text.c_str());
		if (ImGui::InputText(label, buffer, sizeof(buffer))) { value = std::string(buffer); changed = true; }
		break;
	}
	case FieldType::Asset:
	{
		const AssetRef& ref = std::get<AssetRef>(current);
		ImGui::Text("%s: %s", label, describe_ref(ref).c_str());
		if (ref.valid())
		{
			ImGui::SameLine();
			if (ImGui::SmallButton("Clear")) { value = AssetRef{}; changed = true; }
		}
		break;
	}
	case FieldType::AssetList:
		ImGui::Text("%s: %zu asset(s)", label, std::get<std::vector<AssetRef>>(current).size());
		break;
	case FieldType::Entity:
		ImGui::Text("%s: %s", label, std::get<EntityGuid>(current).valid() ? std::get<EntityGuid>(current).to_string().substr(0, 8).c_str() : "(none)");
		break;
	}

	if (overridden)
	{
		ImGui::PopStyleColor();
		ImGui::SameLine();
		if (ImGui::SmallButton("R"))
		{
			const std::string component = info.Name;
			const std::string name = field.Name;
			m_Deferred = [this, entity, component, name]() { m_Editor.revert_field(entity, component, name); };
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Overridden: go back to the prefab's value");
		}
	}
	return changed;
}
