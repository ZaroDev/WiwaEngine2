#pragma once

#include "../scene/scene_editor.h"

// The "Lighting" panel: the ambient light, the background and the exposure of the scene being edited. Main thread, between the ImGui
// frame's begin and end.
class LightingPanel
{
public:
	explicit LightingPanel(wiwa::SceneEditor& editor) : m_Editor(editor) {}

	void draw();

private:
	wiwa::SceneEditor& m_Editor;
};
