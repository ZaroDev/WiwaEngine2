#pragma once

#include "panel.h"
#include <scene/scene_editor.h>

// The "Bloom" panel: the glow around bright parts of the scene being edited. Main thread, between the ImGui frame's begin and end.
class BloomPanel final : public Panel
{
public:
	explicit BloomPanel(wiwa::SceneEditor& editor) : Panel("Bloom", PanelCategory::Renderer), m_Editor(editor) { m_Open = false; }

	virtual void on_draw() override;
private:
	wiwa::SceneEditor& m_Editor;
};
