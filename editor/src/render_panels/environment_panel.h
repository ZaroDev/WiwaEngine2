#pragma once

#include <string>

#include "../scene/scene_editor.h"

// The "Environment" panel: where the light around the scene comes from (an Environment asset or the procedural atmosphere), how it is
// turned and how bright it is, and whether it works. Edits the settings of the scene being edited; Save writes them with the scene.
// Main thread, between the ImGui frame's begin and end.
class EnvironmentPanel
{
public:
	explicit EnvironmentPanel(wiwa::SceneEditor& editor) : m_Editor(editor) {}

	void draw();

private:
	wiwa::SceneEditor& m_Editor;
	char m_Path[260] = "Assets/environments/";
	std::string m_PathStatus{};
	wiwa::AssetGuid m_ShownGuid{}; // the asset the status line was last filled for
};
