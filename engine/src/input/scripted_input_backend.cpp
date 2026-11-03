#include <pch.h>
#include "scripted_input_backend.h"

namespace wiwa
{
	void ScriptedInputBackend::key(Key key, bool down)
	{
		const u32 index = static_cast<u32>(key);
		if (down && !m_State.KeyDown[index]) m_State.KeyPressed.set(index);
		if (!down && m_State.KeyDown[index]) m_State.KeyReleased.set(index);
		m_State.KeyDown.set(index, down);
	}

	void ScriptedInputBackend::mouse_button(MouseButton button, bool down)
	{
		const u32 index = static_cast<u32>(button);
		if (down && !m_State.MouseDown[index]) m_State.MousePressed.set(index);
		if (!down && m_State.MouseDown[index]) m_State.MouseReleased.set(index);
		m_State.MouseDown.set(index, down);
	}

	void ScriptedInputBackend::move_cursor(glm::vec2 position)
	{
		m_State.Cursor = position;
	}

	void ScriptedInputBackend::scroll(glm::vec2 amount)
	{
		m_State.Scroll += amount;
	}

	void ScriptedInputBackend::connect_pad(u32 pad, bool connected)
	{
		if (pad >= MAX_GAMEPADS) return;

		PadState& state = m_State.Pads[pad];
		if (!connected && state.Connected)
		{
			// Unplugged with buttons held: they are released, not stuck.
			state.Released |= state.Down;
			state.Down.reset();
			for (float& axis : state.Axes) axis = 0.0f;
		}
		state.Connected = connected;
	}

	void ScriptedInputBackend::pad_button(u32 pad, PadButton button, bool down)
	{
		if (pad >= MAX_GAMEPADS) return;

		PadState& state = m_State.Pads[pad];
		const u32 index = static_cast<u32>(button);
		if (down && !state.Down[index]) state.Pressed.set(index);
		if (!down && state.Down[index]) state.Released.set(index);
		state.Down.set(index, down);
	}

	void ScriptedInputBackend::pad_axis(u32 pad, PadAxis axis, float value)
	{
		if (pad >= MAX_GAMEPADS) return;
		m_State.Pads[pad].Axes[static_cast<u32>(axis)] = value;
	}

	void ScriptedInputBackend::focus_lost()
	{
		m_State.KeyReleased |= m_State.KeyDown;
		m_State.KeyDown.reset();
		m_State.MouseReleased |= m_State.MouseDown;
		m_State.MouseDown.reset();
		for (PadState& pad : m_State.Pads)
		{
			pad.Released |= pad.Down;
			pad.Down.reset();
		}
	}

	void ScriptedInputBackend::begin_frame(RawInputState& out)
	{
		out = m_State;
		out.CursorDelta = m_State.Cursor - m_ReportedCursor;
		m_ReportedCursor = m_State.Cursor;

		// The edges and the scroll were delivered; the held state stays.
		m_State.KeyPressed.reset();
		m_State.KeyReleased.reset();
		m_State.MousePressed.reset();
		m_State.MouseReleased.reset();
		m_State.Scroll = glm::vec2(0.0f);
		for (PadState& pad : m_State.Pads)
		{
			pad.Pressed.reset();
			pad.Released.reset();
		}
	}
}
