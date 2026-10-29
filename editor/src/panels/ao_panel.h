#pragma once

#include "panel.h"
#include <scene/scene_editor.h>

// The "Ambient occlusion" panel: the darkening of the ambient light in creases and corners of the scene being edited. Main thread,
// between the ImGui frame's begin and end.
class AoPanel final : public Panel
{
public:
	explicit AoPanel(wiwa::SceneEditor& editor) : Panel("Ambient occlusion", PanelCategory::Renderer), m_Editor(editor) { m_Open = false; }

	virtual void on_draw() override;
private:
	wiwa::SceneEditor& m_Editor;
};
