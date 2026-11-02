#include <editor_common.h>
#include "editor_play_input.h"

#include <imgui.h>

namespace wiwa
{
	void EditorPlayInputSource::set_viewport(const glm::vec2& imageMin, float scale, bool hovered)
	{
		m_ImageMin = imageMin;
		m_Scale = scale > 0.0f ? scale : 1.0f;
		m_Hovered = hovered;
	}

	UiInputFrame EditorPlayInputSource::poll()
	{
		const ImGuiIO& io = ImGui::GetIO();

		UiPointerSample sample;
		sample.Pointer = glm::vec2(io.MousePos.x, io.MousePos.y);
		sample.OverImage = m_Hovered;
		for (u32 button = 0; button < UI_POINTER_BUTTONS; button++)
		{
			sample.Down[button] = io.MouseDown[button];
			sample.Pressed[button] = ImGui::IsMouseClicked(static_cast<ImGuiMouseButton>(button));
			sample.Released[button] = ImGui::IsMouseReleased(static_cast<ImGuiMouseButton>(button));
		}
		sample.Scroll = io.MouseWheel;

		const auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key); };
		if (pressed(ImGuiKey_UpArrow)) { sample.NavPressed |= ui_nav_bit(UiNav::Up); }
		if (pressed(ImGuiKey_DownArrow)) { sample.NavPressed |= ui_nav_bit(UiNav::Down); }
		if (pressed(ImGuiKey_LeftArrow)) { sample.NavPressed |= ui_nav_bit(UiNav::Left); }
		if (pressed(ImGuiKey_RightArrow)) { sample.NavPressed |= ui_nav_bit(UiNav::Right); }
		if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)) { sample.NavPressed |= ui_nav_bit(UiNav::Submit); }
		if (pressed(ImGuiKey_Escape)) { sample.NavPressed |= ui_nav_bit(UiNav::Cancel); }
		return convert(sample);
	}

	UiInputFrame EditorPlayInputSource::convert(const UiPointerSample& sample)
	{
		UiInputFrame frame;
		frame.Pointer = (sample.Pointer - m_ImageMin) * m_Scale;

		bool held = false;
		for (u32 button = 0; button < UI_POINTER_BUTTONS; button++)
		{
			held = held || m_Held[button];
			if (sample.Pressed[button] && sample.OverImage)
			{
				m_Held[button] = true;
				held = true;
				frame.Pressed[button] = true;
			}

			frame.Down[button] = sample.Down[button] && m_Held[button];
			frame.Released[button] = sample.Released[button] && m_Held[button];
			if (!sample.Down[button])
			{
				m_Held[button] = false;
			}
		}

		// off the image the pointer still counts while a drag that began on it goes on, and on the frame it ends
		frame.PointerValid = sample.OverImage || held;
		if (sample.OverImage)
		{
			frame.Scroll = sample.Scroll;
			frame.NavPressed = sample.NavPressed;
		}
		return frame;
	}
}
