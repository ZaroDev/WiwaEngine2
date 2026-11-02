#include <pch.h>
#include "ui_input.h"

namespace wiwa
{
	void ScriptedInputSource::move_to(const glm::vec2& pointer)
	{
		m_State.Pointer = pointer;
		m_State.PointerValid = true;
	}

	void ScriptedInputSource::clear_pointer()
	{
		m_State.Pointer = glm::vec2(-1.0f);
		m_State.PointerValid = false;
	}

	void ScriptedInputSource::press(int button)
	{
		if (button >= 0 && static_cast<u32>(button) < UI_POINTER_BUTTONS)
		{
			m_State.Down[button] = true;
			m_State.Pressed[button] = true;
		}
	}

	void ScriptedInputSource::release(int button)
	{
		if (button >= 0 && static_cast<u32>(button) < UI_POINTER_BUTTONS)
		{
			m_State.Down[button] = false;
			m_State.Released[button] = true;
		}
	}

	void ScriptedInputSource::nav(UiNav nav)
	{
		m_State.NavPressed |= ui_nav_bit(nav);
	}

	void ScriptedInputSource::scroll(float amount)
	{
		m_State.Scroll += amount;
	}

	UiInputFrame ScriptedInputSource::poll()
	{
		const UiInputFrame frame = m_State;
		for (u32 i = 0; i < UI_POINTER_BUTTONS; i++)
		{
			m_State.Pressed[i] = false;
			m_State.Released[i] = false;
		}
		m_State.Scroll = 0.0f;
		m_State.NavPressed = 0;
		return frame;
	}
}
