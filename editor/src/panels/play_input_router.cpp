#include "play_input_router.h"

#include <imgui.h>

#include <core/engine.h>
#include <input/input_module.h>

void PlayInputRouter::update(bool playing, const glm::vec2& imageMin, float scale, bool hovered)
{
	wiwa::InputModule* input = wiwa::Engine::get().get_module<wiwa::InputModule>();
	if (input == nullptr || input->backend_injected()) // an injected backend (a smoke test) decides the capture itself
	{
		return;
	}

	if (!playing)
	{
		m_Released = false;
	}
	else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
	{
		m_Released = false;
	}
	else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || (ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_F1, false)))
	{
		m_Released = true;
	}

	input->ui_source().set_viewport(imageMin, scale, hovered);
	input->set_capture(playing && !m_Released && (hovered || ImGui::IsWindowFocused()));
}
