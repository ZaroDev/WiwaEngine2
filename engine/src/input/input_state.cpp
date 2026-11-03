#include <pch.h>
#include "input_state.h"

namespace wiwa
{
	namespace
	{
		// A held axis counts as "down" past this magnitude (the same threshold actions use for Button actions).
		constexpr float AXIS_DOWN_THRESHOLD = 0.5f;

		// A pad axis must pass this to count as the player using the pad (stick noise stays below).
		constexpr float LAST_DEVICE_AXIS = 0.3f;

		enum class Edge { Down, Pressed, Released };

		bool button_state(const RawInputState& raw, bool capture, const InputControl& control, Edge edge)
		{
			switch (control.Device)
			{
			case InputDevice::Key:
				if (!capture || control.Code >= KEY_COUNT) return false;
				return edge == Edge::Down ? raw.KeyDown[control.Code] : edge == Edge::Pressed ? raw.KeyPressed[control.Code] : raw.KeyReleased[control.Code];
			case InputDevice::Mouse:
				if (!capture || control.Code >= MOUSE_BUTTON_COUNT) return false;
				return edge == Edge::Down ? raw.MouseDown[control.Code] : edge == Edge::Pressed ? raw.MousePressed[control.Code] : raw.MouseReleased[control.Code];
			case InputDevice::Pad:
				if (control.Code >= PAD_BUTTON_COUNT) return false;
				for (const PadState& pad : raw.Pads)
				{
					const bool hit = edge == Edge::Down ? pad.Down[control.Code] : edge == Edge::Pressed ? pad.Pressed[control.Code] : pad.Released[control.Code];
					if (hit) return true;
				}
				return false;
			}
			return false;
		}
	}

	void InputState::update(IInputBackend& backend)
	{
		backend.begin_frame(m_Raw);

		// Gamepad first, so a new key press or mouse movement wins over a stick that is simply being held.
		for (const PadState& pad : m_Raw.Pads)
		{
			bool active = pad.Pressed.any();
			for (float axis : pad.Axes)
			{
				active = active || std::abs(axis) > LAST_DEVICE_AXIS;
			}
			if (pad.Connected && active) m_LastDevice = LastDevice::Gamepad;
		}
		if (m_Capture && (m_Raw.KeyPressed.any() || m_Raw.MousePressed.any() || m_Raw.CursorDelta != glm::vec2(0.0f)))
		{
			m_LastDevice = LastDevice::KeyboardMouse;
		}
	}

	float InputState::value(const InputControl& control) const
	{
		if (!control_is_axis(control))
		{
			return down(control) ? 1.0f : 0.0f;
		}

		if (control.Device == InputDevice::Mouse)
		{
			if (!m_Capture) return 0.0f;
			switch (static_cast<MouseAxis>(control.Code - MOUSE_AXIS_CODE_BASE))
			{
			case MouseAxis::DeltaX: return m_Raw.CursorDelta.x;
			case MouseAxis::DeltaY: return m_Raw.CursorDelta.y;
			case MouseAxis::ScrollX: return m_Raw.Scroll.x;
			case MouseAxis::ScrollY: return m_Raw.Scroll.y;
			default: return 0.0f;
			}
		}

		const u32 axis = control.Code - PAD_BUTTON_COUNT;
		if (axis >= PAD_AXIS_COUNT) return 0.0f;

		float best = 0.0f;
		for (const PadState& pad : m_Raw.Pads)
		{
			if (pad.Connected && std::abs(pad.Axes[axis]) > std::abs(best))
			{
				best = pad.Axes[axis];
			}
		}
		return best;
	}

	bool InputState::down(const InputControl& control) const
	{
		if (control_is_axis(control))
		{
			return std::abs(value(control)) >= AXIS_DOWN_THRESHOLD;
		}
		return button_state(m_Raw, m_Capture, control, Edge::Down);
	}

	bool InputState::pressed(const InputControl& control) const
	{
		return !control_is_axis(control) && button_state(m_Raw, m_Capture, control, Edge::Pressed);
	}

	bool InputState::released(const InputControl& control) const
	{
		return !control_is_axis(control) && button_state(m_Raw, m_Capture, control, Edge::Released);
	}
}
