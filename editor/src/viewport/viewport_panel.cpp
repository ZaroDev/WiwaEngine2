#include "viewport_panel.h"

#include <algorithm>

#include <imgui.h>

#include <core/engine.h>
#include <renderer/render_module.h>

void ViewportPanel::draw()
{
	wiwa::RenderModule* render = wiwa::Engine::get().get_module<wiwa::RenderModule>();
	if (render == nullptr || !render->is_ready())
	{
		return;
	}

	ImGui::SetNextWindowPos(ImVec2(384.0f, 12.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(800.0f, 540.0f), ImGuiCond_FirstUseEver);
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
		}
	}
	ImGui::End();
}
