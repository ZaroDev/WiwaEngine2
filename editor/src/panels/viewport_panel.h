#pragma once

#include "panel.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <gizmo/gizmo_settings.h>
#include <gizmo/scene_gizmos.h>
#include <renderer/scene/editor_camera.h>
#include <scene/scene_editor.h>


// The "Viewport" panel: the scene as the renderer draws it, filling the window. It tells the renderer how big its content region is
// (the scene is rendered at exactly that size) and shows the result as an image. It also flies a camera of its own: hold the right
// mouse button over the panel and look around with the mouse, WASD to move, Q/E down and up, Shift for speed, the wheel changes the
// speed. With "Editor camera" off the scene's primary camera is used. Main thread, between the ImGui frame's begin and end.
//
// A left click on the image selects the entity under the mouse (answered by the renderer a frame or two later); the selection is outlined.
// The selected entity has a transform gizmo, ImGuizmo's (editor camera only): W / E / R switch between move, rotate and scale, Ctrl snaps.
// A grid on the ground, lights, cameras and the bounds of the selection are shown too. All of them are on by default; the "Gizmos"
// button switches each of them off and sets the look of the outline (kept in gizmo_settings.json).
class ViewportPanel : public Panel
{
public:
	ViewportPanel(wiwa::SceneEditor& editor);

	virtual void on_draw() override;

private:
	void apply_manipulation(entt::entity entity, const glm::mat4& world);
	void update_game_input(const glm::vec2& imageMin, float scale, bool hovered);

	wiwa::SceneEditor& m_Editor;
	int m_Operation = ImGuizmo::TRANSLATE;
	bool m_LocalSpace = false; // the transform gizmo works in the entity's own axes (scale always does)
	wiwa::SceneGizmos m_SceneGizmos;
	wiwa::GizmoSettings m_GizmoSettings{};
	bool m_GizmoSettingsChanged = false; // saved once the mouse is let go
	wiwa::EditorCamera m_Camera{};
	bool m_UseEditorCamera = true;
	bool m_GameReleased = false; // Escape or Shift+F1 gave the keyboard and the mouse back to the editor while a scene plays
};
