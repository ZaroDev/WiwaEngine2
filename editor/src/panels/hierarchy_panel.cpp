#include <editor_common.h>
#include "hierarchy_panel.h"

#include <imgui.h>

#include <assets/asset_manager.h>
#include <core/engine.h>

namespace
{
	constexpr const char* ENTITY_PAYLOAD = "WI_ENTITY";
	const ImVec4 PREFAB_NODE_COLOR(0.60f, 0.76f, 1.00f, 1.0f);
	const ImVec4 ADDED_CHILD_COLOR(0.60f, 1.00f, 0.65f, 1.0f);
	const ImVec4 INSTANCE_COLOR(1.00f, 0.80f, 0.45f, 1.0f);
}

void HierarchyPanel::on_draw()
{
	ImGui::SetNextWindowPos(ImVec2(12.0f, 250.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(360.0f, 420.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(get_name(), &m_Open) && m_Editor.get_scene() != nullptr)
	{
		draw_toolbar();
		ImGui::Separator();

		wiwa::Scene& scene = m_Editor.scene();
		const std::vector<entt::entity> roots = scene.roots(); // a copy: the tree can change while it is drawn
		for (const entt::entity root : roots)
		{
			draw_node(root);
		}

		// the space under the tree: click to clear the selection, drop an entity to move it to the top level
		const ImVec2 remaining = ImGui::GetContentRegionAvail();
		ImGui::InvisibleButton("##tree_background", ImVec2(-1.0f, std::max(remaining.y, 24.0f)));
		if (ImGui::IsItemClicked())
		{
			m_Editor.select(entt::null);
		}
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ENTITY_PAYLOAD))
			{
				const entt::entity dropped = *static_cast<const entt::entity*>(payload->Data);
				m_Deferred = [this, dropped]() { m_Editor.reparent(dropped, entt::null); };
			}
			ImGui::EndDragDropTarget();
		}

		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Delete) && m_Editor.selected() != entt::null)
		{
			const entt::entity selected = m_Editor.selected();
			m_Deferred = [this, selected]() { m_Editor.delete_entity(selected); };
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

void HierarchyPanel::draw_toolbar()
{
	const std::string name = m_Editor.path().empty() ? "untitled" : m_Editor.path().filename().string();
	ImGui::Text("Scene: %s%s", name.c_str(), m_Editor.dirty() ? " *" : "");

	if (m_Editor.is_opening())
	{
		if (const Ref<wiwa::JobProgress> progress = m_Editor.get_open_progress())
		{
			const std::string stage = progress->stage();
			ImGui::ProgressBar(progress->fraction(), ImVec2(-1.0f, 0.0f), stage.c_str());
		}
	}

	if (ImGui::Button("New"))
	{
		m_Editor.new_scene();
	}
	ImGui::SameLine();
	if (ImGui::Button("Open..."))
	{
		ImGui::OpenPopup("Open scene");
	}
	ImGui::SameLine();
	if (ImGui::Button("Save"))
	{
		if (m_Editor.path().empty())
		{
			ImGui::OpenPopup("Save scene as");
		}
		else if (!m_Editor.save())
		{
			m_Status = "Could not save " + m_Editor.path().string() + " (see the log)";
		}
		else
		{
			m_Status.clear();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Save as..."))
	{
		ImGui::OpenPopup("Save scene as");
	}

	if (ImGui::Button("+ Entity"))
	{
		const entt::entity selected = m_Editor.selected();
		const entt::entity created = m_Editor.create_entity(selected);
		if (created != entt::null)
		{
			m_Editor.select(created);
		}
		else
		{
			m_Status = "Nothing can be added under that entity";
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("+ Prefab..."))
	{
		ImGui::OpenPopup("Add prefab");
	}

	draw_popups();

	if (!m_Status.empty())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_Status.c_str());
	}
}

void HierarchyPanel::draw_popups()
{
	if (ImGui::BeginPopup("Open scene"))
	{
		ImGui::TextUnformatted("Source path of the scene:");
		ImGui::InputText("##open_path", m_OpenPath, sizeof(m_OpenPath));
		if (ImGui::Button("Open"))
		{
			m_Editor.open(m_OpenPath);
			m_Status.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopup("Save scene as"))
	{
		ImGui::TextUnformatted("File to write (a .wscene in Assets/):");
		ImGui::InputText("##save_path", m_SavePath, sizeof(m_SavePath));
		if (ImGui::Button("Save"))
		{
			m_Status = m_Editor.save_as(m_SavePath) ? std::string() : std::string("Could not save ") + m_SavePath + " (see the log)";
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (ImGui::BeginPopup("Add prefab"))
	{
		ImGui::TextUnformatted("Source path of the model:");
		ImGui::InputText("##prefab_path", m_PrefabPath, sizeof(m_PrefabPath));
		if (ImGui::Button("Add"))
		{
			wiwa::AssetManager* assets = wiwa::Engine::get().get_module<wiwa::AssetManager>();
			const std::optional<wiwa::AssetRef> prefab = assets->find_asset(m_PrefabPath);
			if (!prefab)
			{
				m_Status = std::string("No asset at ") + m_PrefabPath + " (is it imported?)";
			}
			else
			{
				const entt::entity selected = m_Editor.selected();
				const entt::entity parent = (selected != entt::null && m_Editor.kind_of(selected) == wiwa::SceneEditor::Kind::Authored) ? selected : entt::entity{ entt::null };
				const entt::entity instance = m_Editor.add_prefab(*prefab, parent);
				m_Editor.select(instance);
				m_Status.clear();
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void HierarchyPanel::draw_node(entt::entity entity)
{
	entt::registry& registry = m_Editor.scene().registry();
	if (!registry.valid(entity))
	{
		return;
	}

	const wiwa::SceneEditor::Kind kind = m_Editor.kind_of(entity);
	const bool isInstance = registry.all_of<wiwa::PrefabInstance>(entity);
	const wiwa::Hierarchy& link = registry.get<wiwa::Hierarchy>(entity);

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
	if (link.FirstChild == entt::null)
	{
		flags |= ImGuiTreeNodeFlags_Leaf;
	}
	if (m_Editor.selected() == entity)
	{
		flags |= ImGuiTreeNodeFlags_Selected;
	}

	const char* color = nullptr;
	ImVec4 textColor;
	if (kind == wiwa::SceneEditor::Kind::PrefabNode)
	{
		textColor = PREFAB_NODE_COLOR;
		color = "";
	}
	else if (kind == wiwa::SceneEditor::Kind::AddedChild)
	{
		textColor = ADDED_CHILD_COLOR;
		color = "";
	}
	else if (isInstance)
	{
		textColor = INSTANCE_COLOR;
		color = "";
	}
	if (color != nullptr)
	{
		ImGui::PushStyleColor(ImGuiCol_Text, textColor);
	}

	const std::string& name = registry.get<wiwa::Name>(entity).Value;
	const bool open = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<uintptr_t>(entt::to_integral(entity))), flags, "%s%s", isInstance ? "[P] " : "", name.empty() ? "(unnamed)" : name.c_str());
	if (color != nullptr)
	{
		ImGui::PopStyleColor();
	}

	if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
	{
		m_Editor.select(entity);
	}

	if (kind == wiwa::SceneEditor::Kind::Authored && ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload(ENTITY_PAYLOAD, &entity, sizeof(entity));
		ImGui::TextUnformatted(name.c_str());
		ImGui::EndDragDropSource();
	}
	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ENTITY_PAYLOAD))
		{
			const entt::entity dropped = *static_cast<const entt::entity*>(payload->Data);
			m_Deferred = [this, dropped, entity]() { m_Editor.reparent(dropped, entity); };
		}
		ImGui::EndDragDropTarget();
	}

	if (ImGui::BeginPopupContextItem())
	{
		m_Editor.select(entity);
		if (ImGui::MenuItem("Create child"))
		{
			m_Deferred = [this, entity]() { if (m_Editor.create_entity(entity) == entt::null) { m_Status = "Nothing can be added under that entity"; } };
		}
		if (isInstance)
		{
			if (ImGui::MenuItem("Unpack prefab"))
			{
				m_Deferred = [this, entity]() { m_Editor.unpack(entity); };
			}
			if (ImGui::MenuItem("Revert all overrides"))
			{
				m_Deferred = [this, entity]() { m_Editor.revert_instance(entity); };
			}
		}
		if (ImGui::MenuItem("Delete"))
		{
			m_Deferred = [this, entity]() { m_Editor.delete_entity(entity); };
		}
		ImGui::EndPopup();
	}

	if (open)
	{
		std::vector<entt::entity> children;
		for (entt::entity child = link.FirstChild; child != entt::null; child = registry.get<wiwa::Hierarchy>(child).NextSibling)
		{
			children.push_back(child);
		}
		for (const entt::entity child : children)
		{
			draw_node(child);
		}
		ImGui::TreePop();
	}
}
