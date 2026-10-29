#pragma once

#include "panel.h"
#include <gizmo/gizmo_settings.h>
#include <gizmo/scene_gizmos.h>
#include <gizmo/transform_gizmo.h>
#include <renderer/scene/editor_camera.h>
#include <scene/scene_editor.h>


// The "Viewport" panel: the scene as the renderer draws it, filling the window. It tells the renderer how big its content region is
// (the scene is rendered at exactly that size) and shows the result as an image. It also flies a camera of its own: hold the right
// mouse button over the panel and look around with the mouse, WASD to move, Q/E down and up, Shift for speed, the wheel changes the
// speed. With "Editor camera" off the scene's primary camera is used. Main thread, between the ImGui frame's begin and end.
//
// A left click on the image selects the entity under the mouse (answered by the renderer a frame or two later); the selection is outlined.
// The selected entity has a transform gizmo (editor camera only): W / E / R switch between move, rotate and scale, Ctrl snaps. Lights,
// cameras and the bounds of the selection are marked too; the "Gizmos" button switches each of them and sets the look of the outline
// (kept in gizmo_settings.json).
class ViewportPanel : public Panel
{
public:
	explicit ViewportPanel(wiwa::SceneEditor& editor);

	virtual void on_draw() override;

private:
	wiwa::SceneEditor& m_Editor;
	wiwa::TransformGizmo m_Gizmo{};
	wiwa::SceneGizmos m_SceneGizmos;
	wiwa::GizmoSettings m_GizmoSettings{};
	bool m_GizmoSettingsChanged = false; // saved once the mouse is let go
	wiwa::EditorCamera m_Camera{};
	bool m_UseEditorCamera = true;
};
