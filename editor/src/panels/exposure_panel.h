#pragma once

#include "panel.h"
#include <scene/scene_editor.h>

// The "Exposure" panel: the manual exposure of the scene being edited, or the automatic one (eye adaptation) with its limits and speed.
// Main thread, between the ImGui frame's begin and end.
class ExposurePanel final : public Panel
{
public:
	explicit ExposurePanel(wiwa::SceneEditor& editor) : Panel("Exposure", PanelCategory::Renderer), m_Editor(editor) { m_Open = false; }

	virtual void on_draw() override;
private:
	wiwa::SceneEditor& m_Editor;
};
