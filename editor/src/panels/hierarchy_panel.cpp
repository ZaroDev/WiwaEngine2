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
	const ImVec4 ERROR_COLOR(1.0f, 0.4f, 0.4f, 1.0f);
}

void HierarchyPanel::on_draw()
{
	ImGui::SetNextWindowPos(ImVec2(12.0f, 250.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(360.0f, 420.0f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin(get_name(), &m_Open) && m_Editor.get_scene() != nullptr)
	{
		draw_header();
		ImGui::Separator();

		wiwa::Scene& scene = m_Editor.scene();
		const std::vector<entt::entity> roots = scene.roots(); // a copy: the tree can change while it is drawn
		for (const entt::entity root : roots)
		{
			draw_node(root);
		}

		// the space under the tree: click to clear the selection, drop an entity to move it to the top level, right-click to add
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
		draw_background_menu();

		draw_popups();

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

void HierarchyPanel::draw_header()
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

	if (!m_Status.empty())
	{
		ImGui::TextColored(ERROR_COLOR, "%s", m_Status.c_str());
	}
}

void HierarchyPanel::draw_background_menu()
{
	if (!ImGui::BeginPopupContextItem("##tree_menu", ImGuiPopupFlags_MouseButtonRight))
	{
		return;
	}

	if (ImGui::MenuItem("New entity"))
	{
		m_Deferred = [this]()
		{
			const entt::entity created = m_Editor.create_entity(entt::null);
			if (created != entt::null)
			{
				m_Editor.select(created);
			}
		};
	}
	if (ImGui::MenuItem("New prefab instance..."))
	{
		m_OpenPrefabPicker = true;
	}
	ImGui::EndPopup();
}

void HierarchyPanel::refresh_prefabs()
{
	m_Prefabs.clear();
	for (const wiwa::AssetDatabase::SourceRoot& root : m_Database.get_roots())
	{
		std::error_code error;
		for (std::filesystem::recursive_directory_iterator it(root.Source, error), end; !error && it != end; it.increment(error))
		{
			const std::filesystem::path extension = it->path().extension();
			if (it->is_regular_file(error) && (extension == ".wprefab" || extension == ".gltf" || extension == ".glb" || extension == ".fbx"))
			{
				m_Prefabs.push_back({ it->path().lexically_relative(root.Source).generic_string(), it->path().string() });
			}
		}
	}
	std::sort(m_Prefabs.begin(), m_Prefabs.end(), [](const PrefabChoice& a, const PrefabChoice& b) { return a.Name < b.Name; });
}

void HierarchyPanel::draw_popups()
{
	if (m_OpenPrefabPicker)
	{
		m_OpenPrefabPicker = false;
		refresh_prefabs();
		ImGui::OpenPopup("Add prefab");
	}
	if (m_OpenRename)
	{
		m_OpenRename = false;
		m_RenameFocus = true;
		ImGui::OpenPopup("Rename entity");
	}

	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("Add prefab", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (m_Prefabs.empty())
		{
			ImGui::TextDisabled("No prefabs or models in the project");
		}
		else if (ImGui::BeginChild("##prefab_list", ImVec2(380.0f, 220.0f), ImGuiChildFlags_Borders))
		{
			for (const PrefabChoice& choice : m_Prefabs)
			{
				if (ImGui::Selectable(choice.Name.c_str()))
				{
					wiwa::AssetManager* assets = wiwa::Engine::get().get_module<wiwa::AssetManager>();
					const std::optional<wiwa::AssetRef> prefab = assets->find_asset(choice.Path);
					if (!prefab)
					{
						m_Status = "No asset at " + choice.Name + " (is it imported?)";
					}
					else
					{
						m_Deferred = [this, ref = *prefab]()
						{
							const entt::entity instance = m_Editor.add_prefab(ref, entt::null);
							if (instance != entt::null)
							{
								m_Editor.select(instance);
							}
						};
						m_Status.clear();
					}
					ImGui::CloseCurrentPopup();
				}
			}
		}
		if (!m_Prefabs.empty())
		{
			ImGui::EndChild();
		}
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	if (ImGui::BeginPopupModal("Rename entity", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		if (m_RenameFocus)
		{
			ImGui::SetKeyboardFocusHere();
			m_RenameFocus = false;
		}
		const bool entered = ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		if (ImGui::Button("OK") || entered)
		{
			const entt::entity target = m_Editor.scene().find(m_RenameTarget);
			if (target != entt::null && !m_Editor.rename_entity(target, m_RenameBuffer))
			{
				m_Status = "That entity cannot be renamed";
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
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
		const bool authored = kind == wiwa::SceneEditor::Kind::Authored;
		if (ImGui::MenuItem("Create child"))
		{
			m_Deferred = [this, entity]()
			{
				const entt::entity created = m_Editor.create_entity(entity);
				if (created == entt::null)
				{
					m_Status = "Nothing can be added under that entity";
				}
				else
				{
					m_Editor.select(created);
				}
			};
		}
		if (ImGui::MenuItem("Duplicate", nullptr, false, authored))
		{
			m_Deferred = [this, entity]() { m_Editor.duplicate_entity(entity); };
		}
		if (ImGui::MenuItem("Rename...", nullptr, false, authored))
		{
			m_RenameTarget = registry.get<wiwa::Id>(entity).Guid;
			std::snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", name.c_str());
			m_OpenRename = true;
		}
		if (isInstance)
		{
			ImGui::Separator();
			if (ImGui::MenuItem("Unpack prefab"))
			{
				m_Deferred = [this, entity]() { m_Editor.unpack(entity); };
			}
			if (ImGui::MenuItem("Revert all overrides"))
			{
				m_Deferred = [this, entity]() { m_Editor.revert_instance(entity); };
			}
		}
		ImGui::Separator();
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
