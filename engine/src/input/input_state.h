#pragma once

#include "input_backend.h"

namespace wiwa
{
	// Which kind of device produced input last: any key, mouse button or cursor movement, or a pad button or an axis past 0.3. For UI prompts.
	enum class LastDevice : u8 { KeyboardMouse, Gamepad };

	// The raw state of the current frame plus the queries over it. Capture gates keyboard and mouse (the editor turns it off while
	// ImGui owns them); gamepads are never gated.
	class InputState
	{
	public:
		void update(IInputBackend& backend);

		void set_capture(bool keyboardMouse) { m_Capture = keyboardMouse; }
		bool capture() const { return m_Capture; }

		bool down(const InputControl& control) const;
		bool pressed(const InputControl& control) const;
		bool released(const InputControl& control) const;
		// Button controls give 0 or 1, axes their raw value. Pad controls use the largest magnitude over the connected pads.
		float value(const InputControl& control) const;

		glm::vec2 mouse_position() const { return m_Capture ? m_Raw.Cursor : glm::vec2(0.0f); }
		glm::vec2 mouse_delta() const { return m_Capture ? m_Raw.CursorDelta : glm::vec2(0.0f); }
		glm::vec2 scroll() const { return m_Capture ? m_Raw.Scroll : glm::vec2(0.0f); }
		bool pad_connected(u32 pad) const { return pad < MAX_GAMEPADS && m_Raw.Pads[pad].Connected; }

		const RawInputState& raw() const { return m_Raw; }

		LastDevice last_device() const { return m_LastDevice; }

	private:
		RawInputState m_Raw;
		bool m_Capture = true;
		LastDevice m_LastDevice = LastDevice::KeyboardMouse;
	};
}
