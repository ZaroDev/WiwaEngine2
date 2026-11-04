#include "game_panel.h"

#include <algorithm>
#include <optional>

#include <imgui.h>

#include <core/engine.h>
#include <renderer/render_module.h>
#include <ui/ui_module.h>

GamePanel::GamePanel(wiwa::SceneEditor& editor, Ref<ViewOwner> owner)
	: Panel("Game", PanelCategory::Window), m_Editor(editor), m_Owner(std::move(owner))
{
}

void GamePanel::on_draw()
{
	wiwa::RenderModule* render = wiwa::Engine::get().get_module<wiwa::RenderModule>();
	if (render == nullptr || !render->is_ready())
	{
		return;
	}

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

		// no room (collapsed, docked away): the render stays with the Viewport
		if (width > 0 && height > 0)
		{
			m_Owner->game_visible();
			render->set_scene_in_viewport(true); // the scene goes to a panel, not over the window
			render->set_viewport_size(width, height);

			// what the game shows: the primary camera, nothing the editor adds
			render->set_camera_override(std::nullopt);
			if (wiwa::UiModule* ui = wiwa::Engine::get().get_module<wiwa::UiModule>())
			{
				ui->set_camera_override(std::nullopt);
			}
			render->get_debug_draw().set_enabled(false);
			render->set_selection(std::nullopt);
			(void)render->take_pick_result(); // a click answered after the Viewport gave way must not change the selection

			ImGui::Image(static_cast<ImTextureID>(render->get_viewport_texture_id()), size);
			const bool hovered = ImGui::IsItemHovered();
			const ImVec2 imageMin = ImGui::GetItemRectMin();
			m_PlayInput.update(m_Editor.playing(), glm::vec2(imageMin.x, imageMin.y), scale, hovered);

			if (!render->get_snapshot().HasCamera)
			{
				ImGui::SetCursorPos(ImVec2(8.0f, 26.0f));
				ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "No camera");
			}
		}
	}
	ImGui::End();
}
