#pragma once

#include "panel.h"
#include <scene/scene_editor.h>

// The "Lighting" panel: the ambient light, the background and the exposure of the scene being edited. Main thread, between the ImGui
// frame's begin and end.
class LightingPanel final : public Panel
{
public:
	explicit LightingPanel(wiwa::SceneEditor& editor) : m_Editor(editor), Panel("Lighting", PanelCategory::Renderer) {}

	virtual void on_draw() override;
private:
	wiwa::SceneEditor& m_Editor;
};
