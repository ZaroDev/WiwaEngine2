#pragma once

#include "panel.h"
#include "play_input_router.h"
#include "view_owner.h"
#include "../settings/editor_section.h"
#include <imgui.h>
#include <ImGuizmo.h>
#include <gizmo/gizmo_settings.h>
#include <gizmo/scene_gizmos.h>
#include <renderer/scene/editor_camera.h>
#include <scene/scene_editor.h>


// The "Viewport" panel: the scene as the renderer draws it, filling the window. It tells the renderer how big its content region is
// (the scene is rendered at exactly that size) and shows the result as an image. It also flies a camera of its own: hold the right
// mouse button over the panel and look around with the mouse, WASD to move, Q/E down and up, Shift for speed, the wheel changes the
// speed. With "Editor camera" off the scene's primary camera is used. While the Game panel is visible it owns the renderer's one view
// and this panel shows a note instead (ViewOwner). Main thread, between the ImGui frame's begin and end.
//
// A left click on the image selects the entity under the mouse (answered by the renderer a frame or two later); the selection is outlined.
// The selected entity has a transform gizmo, ImGuizmo's (editor camera only): W / E / R switch between move, rotate and scale, Ctrl snaps.
// A grid on the ground, lights, cameras and the bounds of the selection are shown too. All of them are on by default; the "Gizmos"
// button switches each of them off and sets the look of the outline (kept in gizmo_settings.json).
class ViewportPanel : public Panel
{
public:
	ViewportPanel(wiwa::SceneEditor& editor, Ref<ViewOwner> owner, Ref<EditorSection> section);

	virtual void on_draw() override;

	// Frames the entity with the editor camera (F, double-click in the Hierarchy): looks at its world position from a distance that
	// fits its scale, keeping the way the camera looks.
	void focus_on(entt::entity entity);

private:
	void apply_manipulation(entt::entity entity, const glm::mat4& world);

	wiwa::SceneEditor& m_Editor;
	Ref<ViewOwner> m_Owner;
	PlayInputRouter m_PlayInput{};
	int m_Operation = ImGuizmo::TRANSLATE;
	bool m_LocalSpace = false; // the transform gizmo works in the entity's own axes (scale always does)
	Ref<EditorSection> m_Section; // the gizmo settings, shared with the Settings page
	wiwa::SceneGizmos m_SceneGizmos;
	wiwa::EditorCamera m_Camera{};
	bool m_UseEditorCamera = true;
	bool m_ShowGameUi = false; // the Scene view leaves the game UI out; the Game panel shows it
};
