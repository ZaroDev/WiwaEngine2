#pragma once

#include <imgui.h>

#include <renderer/render_module.h>
#include <scene/scene_editor.h>

namespace wiwa
{
	// wiwa_editor --viewport-click-test: runs inside the real editor process and clicks in the Viewport the way a user does, by feeding
	// mouse events to ImGui, so that the whole path is exercised: ImGui input, the Viewport panel, the pick request, the render thread, the
	// answer and the selection. It waits for the scene's meshes to load, then clicks a grid of points around the first mesh until a click
	// selects something. It then looks for a handle of the transform gizmo under the mouse, drags it and checks that the selected entity
// moved. It quits with exit code 0 (logging "Viewport click test passed") or 1 (logging why not).
	class ViewportClickTest
	{
	public:
		static bool requested();

		// Called every frame from the Viewport panel with where the image is and the camera it shows. Returns true while the test is running.
		bool update(RenderModule& render, SceneEditor& editor, ImVec2 imageMin, ImVec2 imageSize, const RenderCamera& camera);

	private:
		enum class Stage { Waiting, Move, Press, Release, Answer, Hunt, GizmoPress, GizmoDrag, GizmoRelease, Finished };

		void finish(bool passed, const std::string& why);

		Stage m_Stage = Stage::Waiting;
		u32 m_Frame = 0;       // frames since the test began
		u32 m_StageFrame = 0;  // frames in this stage
		u32 m_Click = 0;       // which point of the grid
		glm::vec2 m_Center{ 0.0f }; // where the mesh is on screen, in pixels of the image
		bool m_HasCenter = false;

		// the gizmo part
		entt::entity m_Entity{ entt::null };
		glm::vec3 m_StartPosition{ 0.0f };
		glm::vec2 m_GizmoCenter{ 0.0f }; // where the selected entity is on screen, in pixels of the image
		float m_HuntOffset = 0.0f;       // pixels to the right of it
		glm::vec2 m_HandlePoint{ 0.0f }; // where a handle was found
		u32 m_Retries = 0;               // frames spent waiting for the pointer to be where it was put
	};
}
