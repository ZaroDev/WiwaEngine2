#pragma once

#include "panel.h"
#include <scene/scene_editor.h>

// The "Post effects" panel: one tab per screen-space effect that works over time (temporal anti-aliasing, motion blur) for the scene
// being edited. Main thread, between the ImGui frame's begin and end.
class PostEffectsPanel final : public Panel
{
public:
	explicit PostEffectsPanel(wiwa::SceneEditor& editor) : Panel("Post effects", PanelCategory::Renderer), m_Editor(editor) { m_Open = false; }

	virtual void on_draw() override;
private:
	wiwa::SceneEditor& m_Editor;
	bool m_ShowVelocity = false;
};
