#pragma once

#include "input_backend.h"

namespace wiwa
{
	// Injected input for smoke checks: the same role as ScriptedInputSource for the UI.
	class ScriptedInputBackend final : public IInputBackend
	{
	public:
		void key(Key key, bool down);
		void mouse_button(MouseButton button, bool down);
		void move_cursor(glm::vec2 position);
		// Where the cursor already is, without a movement: the first frame reports no delta for it.
		void place_cursor(glm::vec2 position);
		void scroll(glm::vec2 amount);
		void connect_pad(u32 pad, bool connected);
		void pad_button(u32 pad, PadButton button, bool down);
		void pad_axis(u32 pad, PadAxis axis, float value);
		// The window lost focus: everything held is released (with release edges).
		void focus_lost();

		virtual void begin_frame(RawInputState& out) override;
		virtual void set_cursor_mode(CursorMode mode) override { m_CursorMode = mode; }
		CursorMode cursor_mode() const { return m_CursorMode; }

	private:
		// The held state plus the edges accumulated since the last begin_frame.
		RawInputState m_State;
		glm::vec2 m_ReportedCursor{ 0.0f };
		CursorMode m_CursorMode = CursorMode::Normal;
	};
}
