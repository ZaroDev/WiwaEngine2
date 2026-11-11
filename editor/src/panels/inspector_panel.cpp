#include <editor_common.h>
#include "inspector_panel.h"
#include "audio_event_picker.h"

#include <imgui.h>

#include "asset_field.h"

#include <scene/reflection/component_registry.h>
#include <core/engine.h>
#include <assets/asset_manager.h>
#include <scene/particle_runtime.h>

namespace
{
	const ImVec4 OVERRIDDEN_COLOR(0.45f, 0.70f, 1.00f, 1.0f);
	const ImVec4 MISSING_COLOR(1.00f, 0.40f, 0.40f, 1.0f);

	bool is_mandatory(const std::string& component)
	{
		return component == "Name" || component == "Transform";
	}
}

InspectorPanel::InspectorPanel(wiwa::SceneEditor& editor, wiwa::AssetDatabase& database)
	: Panel("Inspector", PanelCategory::Window), m_Editor(editor), m_Database(database)
{
	m_AssetManager = wiwa::Engine::get().get_module<wiwa::AssetManager>();
}

void InspectorPanel::on_draw()
{
	ImGui::SetNextWindowPos(ImVec2(984.0f, 12.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(284.0f, 300.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(get_name(), &m_Open) && m_Editor.get_scene() != nullptr)
	{
		if (m_ScopeOpen && !ImGui::IsAnyItemActive())
		{
			m_Editor.end_edit_scope(); // the widget that held the scope went away (the selection changed under it)
			m_ScopeOpen = false;
		}

		entt::entity entity = m_Editor.selected();
		const bool pinned = m_Pinned.valid();
		if (pinned)
		{
			entity = m_Editor.scene().find(m_Pinned);
			if (entity == entt::null)
			{
				m_Pinned = {}; // the pinned entity is gone
			}
		}
		if (entity == entt::null || !m_Editor.scene().registry().valid(entity))
		{
			ImGui::TextDisabled("Nothing selected");
		}
		else
		{
			if (ImGui::SmallButton(m_Pinned.valid() ? ICON_FK_LOCK " Locked" : ICON_FK_UNLOCK " Lock"))
			{
				m_Pinned = m_Pinned.valid() ? wiwa::EntityGuid{} : m_Editor.scene().registry().get<wiwa::Id>(entity).Guid;
			}
			if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
			{
				ImGui::SetTooltip("Keep showing this entity while other entities are selected");
			}
			ImGui::SameLine();
			ImGui::TextDisabled("%s", m_Editor.scene().registry().get<wiwa::Name>(entity).Value.c_str());
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
				if (ImGui::IsWindowAppearing())
				{
					m_AddFilterBuffer[0] = '\0';
					ImGui::SetKeyboardFocusHere();
				}
				ImGui::InputTextWithHint("##add_filter", "Search...", m_AddFilterBuffer, sizeof(m_AddFilterBuffer));
				ImGui::Separator();
				const ImGuiTextFilter filter(m_AddFilterBuffer);
				for (const wiwa::ComponentInfo* info : wiwa::ComponentRegistry::get().all())
				{
					if (info->Name == "Id" || info->Name == "PrefabInstance" || info->Has(m_Editor.scene().registry(), entity) || !filter.PassFilter(info->display_name().c_str()))
					{
						continue;
					}
					if (ImGui::MenuItem(info->display_name().c_str()))
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
	const bool removable = !is_mandatory(info.Name) && info.Name != "PrefabInstance";
	bool visible = true;
	const bool open = removable ? ImGui::CollapsingHeader(info.display_name().c_str(), &visible, ImGuiTreeNodeFlags_DefaultOpen)
	                            : ImGui::CollapsingHeader(info.display_name().c_str(), ImGuiTreeNodeFlags_DefaultOpen);
	if (!visible)
	{
		const std::string name = info.Name;
		m_Deferred = [this, entity, name]() { m_Editor.remove_component(entity, name); };
	}

	if (info.Name != "PrefabInstance" && ImGui::BeginPopupContextItem())
	{
		if (ImGui::MenuItem("Reset to defaults"))
		{
			for (size_t i = 0; i < info.Fields.size(); i++)
			{
				reset_field(entity, info, i);
			}
		}
		if (ImGui::MenuItem("Copy values"))
		{
			ComponentClipboard clipboard;
			clipboard.Component = info.Name;
			for (size_t i = 0; i < info.Fields.size(); i++)
			{
				clipboard.Values.push_back(info.Get(m_Editor.scene().registry(), entity, i));
			}
			m_ComponentClipboard = std::move(clipboard);
		}
		const bool canPaste = m_ComponentClipboard && m_ComponentClipboard->Component == info.Name && m_ComponentClipboard->Values.size() == info.Fields.size();
		if (ImGui::MenuItem("Paste values", nullptr, false, canPaste))
		{
			for (size_t i = 0; i < info.Fields.size(); i++)
			{
				m_Editor.set_field(entity, info.Name, info.Fields[i].Name, m_ComponentClipboard->Values[i]);
			}
		}
		if (removable)
		{
			ImGui::Separator();
			if (ImGui::MenuItem("Remove component"))
			{
				const std::string name = info.Name;
				m_Deferred = [this, entity, name]() { m_Editor.remove_component(entity, name); };
			}
		}
		ImGui::EndPopup();
	}

	if (open)
	{
		if (info.Name == "PrefabInstance")
		{
			// the link to the prefab is not edited here
			const wiwa::PrefabInstance& instance = m_Editor.scene().registry().get<wiwa::PrefabInstance>(entity);
			std::string ignored;
			draw_asset_slot("##prefab", "Prefab", instance.Prefab, asset_context(wiwa::AssetType::Prefab), ignored);
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
				if (is_axis_trio(info, i))
				{
					draw_axis_trio(entity, info, i);
					i += 2;
					ImGui::PopID();
					continue;
				}

				const wiwa::FieldValue current = info.Get(m_Editor.scene().registry(), entity, i);
				wiwa::FieldValue edited = current;
				const bool joint = info.Name == "AnimationSocket" && info.Fields[i].Name == "Joint";
				const bool changed = joint ? draw_joint_picker(entity, std::get<std::string>(current), edited)
				                           : draw_field(entity, info, i, current, edited);
				if (changed)
				{
					m_Editor.set_field(entity, info.Name, info.Fields[i].Name, edited);
				}
				track_edit_scope();

				const wiwa::FieldType type = info.Fields[i].Type;
				if (type != wiwa::FieldType::Asset && type != wiwa::FieldType::AssetList && type != wiwa::FieldType::Entity && ImGui::BeginPopupContextItem("##field_menu"))
				{
					if (ImGui::MenuItem("Reset to default"))
					{
						reset_field(entity, info, i);
					}
					ImGui::EndPopup();
				}
				ImGui::PopID();
			}
			if (info.Name == "ParticleSystem")
			{
				draw_particle_controls(entity);
			}
		}
	}
	ImGui::PopID();
}

// Play, Stop and Restart of a ParticleSystem in edit mode (the game clock is not running, so nothing else would move it). The state is the system's
// ParticleRuntime, which is not saved: it only says whether the editor is previewing this system.
void InspectorPanel::draw_particle_controls(entt::entity entity)
{
	entt::registry& registry = m_Editor.scene().registry();
	if (!registry.all_of<wiwa::ParticleSystem>(entity))
	{
		return;
	}
	ImGui::BeginDisabled(m_Editor.playing()); // while the game plays, scripts and the game clock run the system
	wiwa::ParticleRuntime& runtime = wiwa::particle_runtime_of(registry, entity);
	if (ImGui::Button(runtime.Playing && runtime.Preview ? "Pause" : "Play"))
	{
		if (runtime.Playing && runtime.Preview)
		{
			runtime.Playing = false;
		}
		else
		{
			runtime.Preview = true;
			if (runtime.Finished)
			{
				runtime.restart();
			}
			runtime.Playing = true;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Stop"))
	{
		runtime.Playing = false;
		runtime.Preview = false;
		runtime.ClearCount++; // the particles in the air go with it
		runtime.Time = 0.0f;
		runtime.Finished = false;
		runtime.Remainder.fill(0.0f);
		runtime.PreviousLocal.fill(-1.0f);
	}
	ImGui::SameLine();
	if (ImGui::Button("Restart"))
	{
		runtime.Preview = true;
		runtime.restart();
	}
	ImGui::EndDisabled();
	if (m_Editor.playing())
	{
		ImGui::TextDisabled("Playing the game: the system runs with the game clock");
	}
	else if (runtime.Preview)
	{
		ImGui::TextDisabled("Previewing: %.2f s", runtime.Time);
	}
}

// A drag or typing in a widget is one undo step: the scope opens when the widget is activated and closes when it is released. The
// edits of the frame that activates it are made before the scope matters: the scene settles at the start of the next frame.
void InspectorPanel::track_edit_scope()
{
	if (ImGui::IsItemActivated() && !m_ScopeOpen)
	{
		m_Editor.begin_edit_scope();
		m_ScopeOpen = true;
	}
	if (ImGui::IsItemDeactivated() && m_ScopeOpen)
	{
		m_Editor.end_edit_scope();
		m_ScopeOpen = false;
	}
}

void InspectorPanel::reset_field(entt::entity entity, const wiwa::ComponentInfo& info, size_t index)
{
	m_Editor.set_field(entity, info.Name, info.Fields[index].Name, info.Fields[index].Default);
}

AssetFieldContext InspectorPanel::asset_context(wiwa::AssetType expected) const
{
	AssetFieldContext context;
	context.Expected = expected;
	context.Database = &m_Database;
	context.Locate = m_Locate;
	context.FindReferences = m_FindReferences;
	context.OpenInAssetEditor = m_OpenInAssetEditor;
	return context;
}

bool InspectorPanel::is_axis_trio(const wiwa::ComponentInfo& info, size_t index) const
{
	if (index + 2 >= info.Fields.size())
	{
		return false;
	}
	const std::string& x = info.Fields[index].Name;
	const std::string& y = info.Fields[index + 1].Name;
	const std::string& z = info.Fields[index + 2].Name;
	if (x.size() < 2 || y.size() != x.size() || z.size() != x.size())
	{
		return false;
	}
	const std::string prefix = x.substr(0, x.size() - 1);
	return info.Fields[index].Type == wiwa::FieldType::Bool && info.Fields[index + 1].Type == wiwa::FieldType::Bool && info.Fields[index + 2].Type == wiwa::FieldType::Bool
		&& x.back() == 'X' && y == prefix + "Y" && z == prefix + "Z";
}

// "[x] X  [ ] Y  [ ] Z  LockPosition": the three flags of an axis trio on one line, like the components of a vector.
void InspectorPanel::draw_axis_trio(entt::entity entity, const wiwa::ComponentInfo& info, size_t index)
{
	static const char* const AXES[3] = { "X", "Y", "Z" };
	static const ImVec4 COLORS[3] = { ImVec4(0.90f, 0.35f, 0.35f, 1.0f), ImVec4(0.45f, 0.80f, 0.40f, 1.0f), ImVec4(0.40f, 0.60f, 0.95f, 1.0f) };

	const std::string& first = info.Fields[index].Name;
	const std::string prefix = first.substr(0, first.size() - 1);
	bool overridden = false;
	for (size_t axis = 0; axis < 3; axis++)
	{
		overridden = overridden || m_Editor.is_overridden(entity, info.Name, info.Fields[index + axis].Name);
	}
	if (overridden)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, OVERRIDDEN_COLOR);
	}

	for (size_t axis = 0; axis < 3; axis++)
	{
		ImGui::PushID(static_cast<int>(axis));
		const wiwa::FieldDescriptor& field = info.Fields[index + axis];
		bool value = std::get<bool>(info.Get(m_Editor.scene().registry(), entity, index + axis));
		if (axis > 0)
		{
			ImGui::SameLine();
		}
		ImGui::AlignTextToFramePadding();
		ImGui::TextColored(COLORS[axis], "%s", AXES[axis]);
		ImGui::SameLine(0.0f, 4.0f);
		if (ImGui::Checkbox("##axis", &value))
		{
			m_Editor.set_field(entity, info.Name, field.Name, wiwa::FieldValue(value));
		}
		if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
		{
			ImGui::SetTooltip("%s", field.Meta.Tooltip.empty() ? field.Name.c_str() : field.Meta.Tooltip.c_str());
		}
		ImGui::PopID();
	}
	ImGui::SameLine();
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted(prefix.c_str());

	if (overridden)
	{
		ImGui::PopStyleColor();
		ImGui::SameLine();
		if (ImGui::SmallButton("R"))
		{
			std::vector<std::string> names;
			for (size_t axis = 0; axis < 3; axis++)
			{
				names.push_back(info.Fields[index + axis].Name);
			}
			const std::string component = info.Name;
			m_Deferred = [this, entity, component, names]()
			{
				for (const std::string& name : names)
				{
					m_Editor.revert_field(entity, component, name);
				}
			};
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Overridden: go back to the prefab value");
		}
	}
}

// The Joint of an AnimationSocket: a drop-down of the joints of the skeleton of the Animator above it (a text box when there is none).
bool InspectorPanel::draw_joint_picker(entt::entity entity, const std::string& current, wiwa::FieldValue& value)
{
	using namespace wiwa;
	entt::registry& registry = m_Editor.scene().registry();
	const Animator* animator = nullptr;
	for (entt::entity up = registry.get<Hierarchy>(entity).Parent; up != entt::null && animator == nullptr; up = registry.get<Hierarchy>(up).Parent)
	{
		animator = registry.try_get<Animator>(up);
	}

	AssetManager* assets = Engine::get().get_module<AssetManager>();
	const Handle<SkeletonData> handle = (animator != nullptr && assets != nullptr) ? assets->load<SkeletonData>(animator->Skeleton) : Handle<SkeletonData>{};
	const SkeletonData* skeleton = handle.get();
	bool changed = false;
	if (skeleton == nullptr)
	{
		char buffer[128] = {};
		std::strncpy(buffer, current.c_str(), sizeof(buffer) - 1);
		if (ImGui::InputText("Joint", buffer, sizeof(buffer)))
		{
			value = std::string(buffer);
			changed = true;
		}
		return changed;
	}

	if (ImGui::BeginCombo("Joint", current.empty() ? "(none)" : current.c_str()))
	{
		for (const std::string& name : skeleton->JointNames)
		{
			if (ImGui::Selectable(name.c_str(), name == current))
			{
				value = name;
				changed = true;
			}
		}
		ImGui::EndCombo();
	}
	return changed;
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
		const bool edited = field.Meta.HasRange ? ImGui::SliderInt(label, &v, static_cast<int>(field.Meta.Min), static_cast<int>(field.Meta.Max)) : ImGui::DragInt(label, &v);
		if (edited) { value = static_cast<i32>(v); changed = true; }
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
		if (field.Meta.AudioEvent)
		{
			std::string event = std::get<std::string>(current);
			if (draw_audio_event_picker(label, event))
			{
				value = event;
				changed = true;
			}
			break;
		}
		char buffer[256];
		const std::string& text = std::get<std::string>(current);
		std::snprintf(buffer, sizeof(buffer), "%s", text.c_str());
		if (ImGui::InputText(label, buffer, sizeof(buffer))) { value = std::string(buffer); changed = true; }
		break;
	}
	case FieldType::Asset:
	{
		const AssetRef& ref = std::get<AssetRef>(current);
		std::string dropped;
		switch (draw_asset_slot(label, label, ref, asset_context(field.Meta.Asset), dropped))
		{
		case AssetFieldEvent::Dropped:
		{
			const std::optional<wiwa::AssetRef> found = m_AssetManager != nullptr ? m_AssetManager->find_asset(dropped) : std::nullopt;
			if (found)
			{
				value = *found;
				changed = true;
			}
			break;
		}
		case AssetFieldEvent::Cleared: value = AssetRef{}; changed = true; break;
		default: break;
		}
		break;
	}
	case FieldType::AssetList:
	{
		std::vector<AssetRef> refs = std::get<std::vector<AssetRef>>(current);
		ImGui::Text("%s: %zu asset(s)", label, refs.size());
		for (size_t i = 0; i < refs.size(); i++)
		{
			ImGui::PushID(static_cast<int>(i));
			std::string dropped;
			const std::string slot = std::string(label) + " " + std::to_string(i);
			switch (draw_asset_slot(slot.c_str(), nullptr, refs[i], asset_context(field.Meta.Asset), dropped))
			{
			case AssetFieldEvent::Dropped:
			{
				const std::optional<wiwa::AssetRef> found = m_AssetManager != nullptr ? m_AssetManager->find_asset(dropped) : std::nullopt;
				if (found)
				{
					refs[i] = *found;
					value = refs;
					changed = true;
				}
				break;
			}
			case AssetFieldEvent::Cleared: refs[i] = AssetRef{}; value = refs; changed = true; break;
			default: break;
			}
			ImGui::PopID();
		}
		break;
	}
	case FieldType::Entity:
	{
		const EntityGuid guid = std::get<EntityGuid>(current);
		std::string target = "(none)";
		bool missing = false;
		if (guid.valid())
		{
			const entt::entity found = m_Editor.scene().find(guid);
			if (found != entt::null && m_Editor.scene().registry().valid(found))
			{
				target = m_Editor.scene().registry().get<Name>(found).Value;
			}
			else
			{
				target = "(missing) " + guid.to_string().substr(0, 8);
				missing = true;
			}
		}
		ImGui::Text("%s:", label);
		ImGui::SameLine();
		if (missing)
		{
			ImGui::TextColored(MISSING_COLOR, "%s", target.c_str());
		}
		else
		{
			ImGui::TextUnformatted(target.c_str());
		}
		break;
	}
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
