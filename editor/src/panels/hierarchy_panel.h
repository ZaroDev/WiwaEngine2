#pragma once

#include <functional>
#include <string>
#include <vector>
#include <imgui.h>

#include "panel.h"

#include <asset_pipeline/asset_database.h>
#include <scene/scene_editor.h>

// The "Hierarchy" panel: the name of the scene being edited and its entity tree. Right-click on an entity or on the empty space to add
// entities and prefab instances, and to copy, rename and delete them (the scene's own file actions are in the File menu). Entities of a
// prefab instance are shown in colors and can be edited through the Inspector (their edits become overrides). Main thread, between the
// ImGui frame's begin and end.
class HierarchyPanel : public Panel
{
public:
	HierarchyPanel(wiwa::SceneEditor& editor, wiwa::AssetDatabase& database)
		: Panel("Hierarchy", PanelCategory::Window), m_Editor(editor), m_Database(database) {}

	virtual void on_draw() override;

	// What the shortcuts do (the editor registers them): rename the selected entity, copy/cut/paste it, frame it in the Viewport.
	void request_rename();
	void copy_selection();
	void cut_selection();
	void paste();
	void focus_selection();
	// The Viewport frames an entity (F, double-click).
	void set_focus(std::function<void(entt::entity)> focus) { m_Focus = std::move(focus); }

private:
	struct PrefabChoice
	{
		std::string Name{}; // relative to its source root
		std::string Path{}; // as the asset catalog knows it
	};

	void draw_header();
	void draw_popups();
	void draw_node(entt::entity entity);
	// The templates of the Create menu (and "New prefab instance...") under parent; the new entity is selected.
	void draw_create_menu(entt::entity parent);
	void drop_asset(const std::string& path, entt::entity target);
	// Whether the entity or something below it passes the search box.
	bool passes_filter(entt::entity entity) const;
	bool filtering() const { return m_Filter.IsActive(); }
	void draw_background_menu();
	// The prefab files and models of the project, for the "Add prefab" popup.
	void refresh_prefabs();

	wiwa::SceneEditor& m_Editor;
	wiwa::AssetDatabase& m_Database;
	std::function<void()> m_Deferred{}; // a change to the tree, done after the tree has been drawn
	std::string m_Status{};             // the last thing that did not work

	bool m_OpenPrefabPicker = false;    // popups are opened from the panel, not from inside the context menu that asked for them
	std::vector<PrefabChoice> m_Prefabs{};

	ImGuiTextFilter m_Filter{};
	bool m_FilterComponents = false; // the search also matches component names
	std::function<void(entt::entity)> m_Focus{};

	bool m_OpenRename = false;
	bool m_RenameFocus = false;
	wiwa::EntityGuid m_RenameTarget{};
	char m_RenameBuffer[128] = "";
};
