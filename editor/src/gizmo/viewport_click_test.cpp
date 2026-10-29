#include <editor_common.h>
#include "viewport_click_test.h"

#include <ImGuizmo.h>
#include <core/engine.h>
#include <scene/components.h>

#define WI_CLICK_LOG(level, ...) WI_LOG(level, "Viewport", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		constexpr u32 MIN_WAIT_FRAMES = 90;
		constexpr u32 MAX_WAIT_FRAMES = 1500;
		constexpr u32 ANSWER_FRAMES = 24;   // how long a click is given to be answered
		constexpr int GRID = 7;             // GRID x GRID clicks
		constexpr float SPACING = 26.0f;    // pixels between clicks
		constexpr float HUNT_STEP = 6.0f;   // pixels between the places the mouse looks for a handle
		constexpr float HUNT_REACH = 170.0f;
		constexpr float DRAG_PIXELS = 80.0f;
		constexpr u32 MAX_RETRIES = 600;    // frames given to a real mouse that keeps moving the pointer away
	}

	bool ViewportClickTest::requested()
	{
		for (const std::string& argument : Engine::get().get_arguments())
		{
			if (argument == "--viewport-click-test")
			{
				return true;
			}
		}
		return false;
	}

	void ViewportClickTest::finish(bool passed, const std::string& why)
	{
		m_Stage = Stage::Finished;
		if (passed)
		{
			WI_CLICK_LOG(Info, "Viewport click test passed: {0}", why);
		}
		else
		{
			WI_CLICK_LOG(Error, "Viewport click test FAILED: {0}", why);
		}
		Engine::get().set_exit_code(passed ? 0 : 1);
		Engine::get().request_quit();
	}

	bool ViewportClickTest::update(RenderModule& render, SceneEditor& editor, ImVec2 imageMin, ImVec2 imageSize, const RenderCamera& camera)
	{
		if (m_Stage == Stage::Finished)
		{
			return false;
		}

		m_Frame++;
		m_StageFrame++;
		ImGuiIO& io = ImGui::GetIO();
		const float scale = io.DisplayFramebufferScale.x;

		auto next = [&](Stage stage) { m_Stage = stage; m_StageFrame = 0; };
		// The real mouse, if it moves while the test runs, can take the pointer away from the events fed here: wait until it is where it was put.
		auto pointer_is_at = [&](const ImVec2& at) { return std::abs(io.MousePos.x - at.x) <= 1.5f && std::abs(io.MousePos.y - at.y) <= 1.5f; };

		if (m_Stage == Stage::Waiting)
		{
			const RenderSnapshot snapshot = render.get_snapshot();
			const bool loaded = snapshot.SceneInstances > 0 && snapshot.CachePending == 0 && snapshot.VisibleInstances > 0;
			if (m_Frame > MAX_WAIT_FRAMES)
			{
				finish(false, "the scene's meshes never became visible (instances " + std::to_string(snapshot.SceneInstances) + ", visible " + std::to_string(snapshot.VisibleInstances)
					+ ", pending " + std::to_string(snapshot.CachePending) + ")");
				return false;
			}
			if (!loaded || m_Frame < MIN_WAIT_FRAMES)
			{
				return true;
			}

			// Where the first mesh is on screen.
			entt::registry& registry = editor.scene().registry();
			u32 meshes = 0;
			float firstW = 0.0f;
			for (auto [entity, mesh, world] : registry.view<MeshRenderer, WorldTransform>().each())
			{
				firstW = meshes++ == 0 ? (camera.Projection * camera.View * world.Matrix[3]).w : firstW;
				const glm::vec4 clip = camera.Projection * camera.View * world.Matrix[3];
				if (clip.w > 0.0f)
				{
					const glm::vec3 ndc = glm::vec3(clip) / clip.w;
					m_Center = glm::vec2((ndc.x * 0.5f + 0.5f) * imageSize.x, (1.0f - (ndc.y * 0.5f + 0.5f)) * imageSize.y);
					m_HasCenter = true;
					break;
				}
			}
			if (!m_HasCenter)
			{
				finish(false, "no mesh is in front of the camera (" + std::to_string(meshes) + " MeshRenderer entities in the editor scene of " + std::to_string(registry.storage<entt::entity>().size()) + " entities, w of the first " + std::to_string(firstW) + ")");
				return false;
			}
			WI_CLICK_LOG(Info, "Viewport click test: the mesh is at ({0:.0f}, {1:.0f}) of a {2:.0f}x{3:.0f} image, the display scale is {4}", m_Center.x, m_Center.y, imageSize.x, imageSize.y, scale);
			next(Stage::Move);
			return true;
		}

		if (m_Stage == Stage::Hunt || m_Stage == Stage::GizmoPress || m_Stage == Stage::GizmoDrag || m_Stage == Stage::GizmoRelease)
		{
			const auto point = [&](const glm::vec2& at) { return ImVec2(imageMin.x + at.x, imageMin.y + at.y); };

			if (m_Stage == Stage::Hunt)
			{
				// Move along the row of the entity, a step at a time, until a handle of the gizmo is under the mouse.
				const glm::vec2 here = m_GizmoCenter + glm::vec2(m_HuntOffset, 0.0f);
				io.AddMousePosEvent(point(here).x, point(here).y);
				if (m_StageFrame >= 4 && !pointer_is_at(point(here)) && ++m_Retries < MAX_RETRIES)
				{
					m_StageFrame = 2; // not there yet
				}
				else if (m_StageFrame >= 4)
				{
					if (ImGuizmo::IsOver())
					{
						m_HandlePoint = here;
						WI_CLICK_LOG(Info, "Viewport click test: a gizmo handle is under the mouse {0:.0f} px from the entity", m_HuntOffset);
						next(Stage::GizmoPress);
					}
					else if (m_HuntOffset >= HUNT_REACH)
					{
						finish(false, "no handle of the transform gizmo was under the mouse anywhere within " + std::to_string(static_cast<int>(HUNT_REACH)) + " px of the selected entity (selected still the same: "
							+ std::to_string(editor.selected() == m_Entity) + ", mouse " + std::to_string(io.MousePos.x) + "," + std::to_string(io.MousePos.y) + ", window hovered " + std::to_string(ImGui::IsWindowHovered())
							+ ", using " + std::to_string(ImGuizmo::IsUsing()) + ", retries " + std::to_string(m_Retries) + ", entity at " + std::to_string(m_GizmoCenter.x) + "," + std::to_string(m_GizmoCenter.y) + ")");
						return false;
					}
					else
					{
						m_HuntOffset += HUNT_STEP;
						m_StageFrame = 0;
					}
				}
				return true;
			}

			if (m_Stage == Stage::GizmoPress)
			{
				io.AddMousePosEvent(point(m_HandlePoint).x, point(m_HandlePoint).y);
				if (m_StageFrame == 1 && !pointer_is_at(point(m_HandlePoint)) && ++m_Retries < MAX_RETRIES)
				{
					m_StageFrame = 0; // not there yet: try again before pressing
				}
				if (m_StageFrame == 1)
				{
					io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
				}
				if (m_StageFrame >= 4)
				{
					next(Stage::GizmoDrag);
				}
				return true;
			}

			if (m_Stage == Stage::GizmoDrag)
			{
				const float travelled = std::min(static_cast<float>(m_StageFrame) * (DRAG_PIXELS / 12.0f), DRAG_PIXELS);
				const glm::vec2 at = m_HandlePoint + glm::vec2(travelled, 0.0f);
				io.AddMousePosEvent(point(at).x, point(at).y);
				if (m_StageFrame >= 16)
				{
					next(Stage::GizmoRelease);
				}
				return true;
			}

			// GizmoRelease
			io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			if (m_StageFrame >= 8)
			{
				const Transform& transform = editor.scene().registry().get<Transform>(m_Entity);
				const float moved = glm::length(transform.Position - m_StartPosition);
				if (moved > 1e-3f)
				{
					finish(true, "the click selected an entity and dragging its gizmo moved it by " + std::to_string(moved));
				}
				else
				{
					finish(false, "the click selected an entity, but dragging a handle of its gizmo did not move it");
				}
				return false;
			}
			return true;
		}

		if (m_Click >= static_cast<u32>(GRID * GRID))
		{
			finish(false, "none of " + std::to_string(GRID * GRID) + " clicks around the mesh selected anything");
			return false;
		}

		const int column = static_cast<int>(m_Click) % GRID - GRID / 2;
		const int row = static_cast<int>(m_Click) / GRID - GRID / 2;
		const ImVec2 point(imageMin.x + m_Center.x + static_cast<float>(column) * SPACING, imageMin.y + m_Center.y + static_cast<float>(row) * SPACING);

		switch (m_Stage)
		{
		case Stage::Move:
			io.AddMousePosEvent(point.x, point.y);
			if (m_StageFrame >= 3)
			{
				next(Stage::Press);
			}
			break;
		case Stage::Press:
			io.AddMousePosEvent(point.x, point.y);
			if (m_StageFrame == 1)
			{
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			}
			if (m_StageFrame >= 3)
			{
				next(Stage::Release);
			}
			break;
		case Stage::Release:
			io.AddMousePosEvent(point.x, point.y);
			if (m_StageFrame == 1)
			{
				io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			}
			if (m_StageFrame >= 2)
			{
				next(Stage::Answer);
			}
			break;
		case Stage::Answer:
			if (editor.selected() != entt::null)
			{
				WI_CLICK_LOG(Info, "Viewport click test: click {0} at offset ({1}, {2}) selected an entity", m_Click + 1, column, row);
				m_Entity = editor.selected();
				const entt::registry& registry = editor.scene().registry();
				m_StartPosition = registry.get<Transform>(m_Entity).Position;
				const glm::vec4 clip = camera.Projection * camera.View * registry.get<WorldTransform>(m_Entity).Matrix[3];
				if (clip.w <= 0.0f)
				{
					finish(false, "the selected entity is behind the camera");
					return false;
				}
				const glm::vec3 ndc = glm::vec3(clip) / clip.w;
				m_GizmoCenter = glm::vec2((ndc.x * 0.5f + 0.5f) * imageSize.x, (1.0f - (ndc.y * 0.5f + 0.5f)) * imageSize.y);
				m_HuntOffset = HUNT_STEP;
				next(Stage::Hunt);
				return true;
			}
			if (m_StageFrame >= ANSWER_FRAMES)
			{
				m_Click++;
				next(Stage::Move);
			}
			break;
		default:
			break;
		}
		return true;
	}
}
