#include "viewport_panel.h"

#include <algorithm>
#include <optional>

#include <imgui.h>

#include <core/engine.h>
#include <renderer/render_module.h>

namespace
{
	float key_axis(ImGuiKey positive, ImGuiKey negative)
	{
		return (ImGui::IsKeyDown(positive) ? 1.0f : 0.0f) - (ImGui::IsKeyDown(negative) ? 1.0f : 0.0f);
	}
}

void ViewportPanel::draw()
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
	const bool open = ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
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
			if (!render->get_snapshot().HasCamera)
			{
				ImGui::SetCursorPos(ImVec2(8.0f, 50.0f));
				ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "No camera");
			}
		}
	}
	ImGui::End();
}
