#pragma once

#include <functional>
#include <string>
#include "panel.h"

#include <scene/scene_editor.h>

// The "Hierarchy" panel: the entity tree of the scene being edited, and the scene's file actions (new, open, save, add an entity or
// a prefab). Entities of a prefab instance are shown in colors and can be edited through the Inspector (their edits become
// overrides). Main thread, between the ImGui frame's begin and end.
class HierarchyPanel : public Panel
{
public:
	explicit HierarchyPanel(wiwa::SceneEditor& editor) : m_Editor(editor), Panel("Hierarchy", PanelCategory::Window) {}

	virtual void on_draw() override;

private:
	void draw_toolbar();
	void draw_popups();
	void draw_node(entt::entity entity);

	wiwa::SceneEditor& m_Editor;
	std::function<void()> m_Deferred{}; // a change to the tree, done after the tree has been drawn
	std::string m_Status{};             // the last thing that did not work
	char m_OpenPath[260] = "assets/scenes/";
	char m_SavePath[260] = "assets/scenes/untitled.wscene";
	char m_PrefabPath[260] = "assets/models/";
};
