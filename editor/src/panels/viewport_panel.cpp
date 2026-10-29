#include "viewport_panel.h"

#include <algorithm>
#include <optional>

#include <imgui.h>

#include <core/engine.h>
#include <gizmo/viewport_selection.h>
#include <renderer/render_module.h>

namespace
{
	float key_axis(ImGuiKey positive, ImGuiKey negative)
	{
		return (ImGui::IsKeyDown(positive) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(negative) ? 1.0f : 0.0f);
	}
}

void ViewportPanel::on_draw()
{
	wiwa::RenderModule* render = wiwa::Engine::get().get_module<wiwa::RenderModule>();
	if (render == nullptr || !render->is_ready())
	{
		return;
	}

	render->set_scene_in_viewport(true); // the scene goes to this panel, not over the window

	ImGui::SetNextWindowPos(ImVec2(376.0f, 12.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(600.0f, 540.0f), ImGuiCond_FirstUseEver);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	const bool open = ImGui::Begin(get_name(), nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PopStyleVar();

	if (open)
	{
		const ImVec2 size = ImGui::GetContentRegionAvail();
		const float scale = ImGui::GetIO().DisplayFramebufferScale.x;
		const wiwa::u32 width = static_cast<wiwa::u32>(std::max(size.x * scale, 0.0f));
		const wiwa::u32 height = static_cast<wiwa::u32>(std::max(size.y * scale, 0.0f));

		// 0 means the panel has no room (collapsed, docked away): the renderer keeps what it has.
		render->set_viewport_size(width, height);

		if (width > 0 && height > 0)
		{
			ImGui::Image(static_cast<ImTextureID>(render->get_viewport_texture_id()), size);
			const bool hovered = ImGui::IsItemHovered();
			const ImVec2 imageMin = ImGui::GetItemRectMin();

			if (m_UseEditorCamera)
			{
				const ImGuiIO& io = ImGui::GetIO();
				const bool looking = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right);

				wiwa::EditorCameraInput input;
				if (looking)
				{
					input.Look = glm::vec2(io.MouseDelta.x, io.MouseDelta.y);
					input.Move = glm::vec3(key_axis(ImGuiKey_D, ImGuiKey_A), key_axis(ImGuiKey_E, ImGuiKey_Q), key_axis(ImGuiKey_W, ImGuiKey_S));
					input.Fast = io.KeyShift;
				}
				if (hovered)
				{
					input.Scroll = io.MouseWheel;
				}
				m_Camera.apply(io.DeltaTime, input);
				render->set_camera_override(m_Camera.get(size.x / std::max(size.y, 1.0f)));
			}
			else
			{
				render->set_camera_override(std::nullopt);
			}

			// Over the image, in its corner.
			ImGui::SetCursorPos(ImVec2(8.0f, 26.0f));
			ImGui::Checkbox("Editor camera", &m_UseEditorCamera);
			ImGui::SameLine();
			if (ImGui::RadioButton("Move (W)", m_Gizmo.mode == wiwa::gizmo::Mode::Translate)) { m_Gizmo.mode = wiwa::gizmo::Mode::Translate; }
			ImGui::SameLine();
			if (ImGui::RadioButton("Rotate (E)", m_Gizmo.mode == wiwa::gizmo::Mode::Rotate)) { m_Gizmo.mode = wiwa::gizmo::Mode::Rotate; }
			ImGui::SameLine();
			if (ImGui::RadioButton("Scale (R)", m_Gizmo.mode == wiwa::gizmo::Mode::Scale)) { m_Gizmo.mode = wiwa::gizmo::Mode::Scale; }
			ImGui::SameLine();
			bool local = m_Gizmo.space == wiwa::gizmo::Space::Local;
			if (ImGui::Checkbox("Local", &local)) { m_Gizmo.space = local ? wiwa::gizmo::Space::Local : wiwa::gizmo::Space::World; }
			if (!render->get_snapshot().HasCamera)
			{
				ImGui::SetCursorPos(ImVec2(8.0f, 50.0f));
				ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "No camera");
			}

			// Selection: what the renderer outlines, and the answer to the last click.
			render->set_selection(wiwa::selected_guid(m_Editor));
			if (const std::optional<wiwa::PickResult> result = render->take_pick_result())
			{
				wiwa::apply_pick(m_Editor, *result);
			}

			// The mouse belongs to the image when it is over it, not over a widget drawn on it, and not flying the camera.
			const ImGuiIO& io = ImGui::GetIO();
			const bool flying = hovered && ImGui::IsMouseDown(ImGuiMouseButton_Right);
			const bool overImage = hovered && !flying && !ImGui::IsAnyItemHovered();

			if (overImage && !ImGui::IsAnyItemActive())
			{
				if (ImGui::IsKeyPressed(ImGuiKey_W, false)) { m_Gizmo.mode = wiwa::gizmo::Mode::Translate; }
				if (ImGui::IsKeyPressed(ImGuiKey_E, false)) { m_Gizmo.mode = wiwa::gizmo::Mode::Rotate; }
				if (ImGui::IsKeyPressed(ImGuiKey_R, false)) { m_Gizmo.mode = wiwa::gizmo::Mode::Scale; }
			}

			const bool clicked = overImage && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
			bool gizmoHasMouse = false;
			if (m_UseEditorCamera)
			{
				const wiwa::RenderCamera camera = m_Camera.get(size.x / std::max(size.y, 1.0f));
				wiwa::GizmoInput input;
				input.MousePixel = glm::vec2(io.MousePos.x - imageMin.x, io.MousePos.y - imageMin.y);
				input.ViewportSize = glm::vec2(size.x, size.y);
				input.Hovered = overImage;
				input.LeftPressed = clicked;
				input.LeftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
				input.Ctrl = io.KeyCtrl;
				input.View = camera.View;
				input.Projection = camera.Projection;
				gizmoHasMouse = m_Gizmo.update(render->get_debug_draw(), input, m_Editor);
			}

			if (clicked && !gizmoHasMouse)
			{
				const float x = std::max(io.MousePos.x - imageMin.x, 0.0f) * scale;
				const float y = std::max(io.MousePos.y - imageMin.y, 0.0f) * scale;
				render->request_pick(static_cast<wiwa::u32>(x), static_cast<wiwa::u32>(y));
			}
		}
	}
	ImGui::End();
}
