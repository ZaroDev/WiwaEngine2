#pragma once

#include "panel.h"
#include <scene/scene_editor.h>

// The "Tonemap" panel: the tone mapping operator and the color grading of the scene being edited. Main thread, between the ImGui
// frame's begin and end.
class TonemapPanel final : public Panel
{
public:
	explicit TonemapPanel(wiwa::SceneEditor& editor) : Panel("Tonemap", PanelCategory::Renderer), m_Editor(editor) { m_Open = false; }

	virtual void on_draw() override;
private:
	wiwa::SceneEditor& m_Editor;
};
