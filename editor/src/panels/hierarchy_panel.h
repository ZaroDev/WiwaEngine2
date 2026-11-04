#pragma once

#include <functional>
#include <string>
#include <vector>
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

private:
	struct PrefabChoice
	{
		std::string Name{}; // relative to its source root
		std::string Path{}; // as the asset catalog knows it
	};

	void draw_header();
	void draw_popups();
	void draw_node(entt::entity entity);
	void draw_background_menu();
	// The prefab files and models of the project, for the "Add prefab" popup.
	void refresh_prefabs();

	wiwa::SceneEditor& m_Editor;
	wiwa::AssetDatabase& m_Database;
	std::function<void()> m_Deferred{}; // a change to the tree, done after the tree has been drawn
	std::string m_Status{};             // the last thing that did not work

	bool m_OpenPrefabPicker = false;    // popups are opened from the panel, not from inside the context menu that asked for them
	std::vector<PrefabChoice> m_Prefabs{};

	bool m_OpenRename = false;
	bool m_RenameFocus = false;
	wiwa::EntityGuid m_RenameTarget{};
	char m_RenameBuffer[128] = "";
};
