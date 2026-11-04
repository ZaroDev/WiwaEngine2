#pragma once

#include "panel.h"
#include "play_input_router.h"
#include "view_owner.h"
#include <scene/scene_editor.h>

// The "Game" panel: the scene as the game shows it, seen by the scene's primary camera, with no debug draw, no gizmos, no selection outline and
// no picking. A scene is played in it: it takes the keyboard, the mouse and the UI pointer like the Viewport does (see PlayInputRouter).
//
// It shares the renderer's one view with the Viewport panel (ViewOwner): while this panel is visible it owns the render and the Viewport
// shows a note instead. Docked as tabs only the visible tab renders. Main thread, between the ImGui frame's begin and end.
class GamePanel : public Panel
{
public:
	GamePanel(wiwa::SceneEditor& editor, Ref<ViewOwner> owner);

	virtual void on_draw() override;

private:
	wiwa::SceneEditor& m_Editor;
	Ref<ViewOwner> m_Owner;
	PlayInputRouter m_PlayInput{};
};
