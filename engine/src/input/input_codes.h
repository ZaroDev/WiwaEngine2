#pragma once

#include <math/math.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wiwa
{
	// Our own control codes; the GLFW backend translates to and from these. The values are never serialized (names are).
	enum class InputDevice : u8 { Key, Mouse, Pad };

	enum class Key : u16
	{
		A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
		Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
		Space, Enter, Escape, Tab, Backspace,
		Left, Right, Up, Down,
		LeftShift, LeftCtrl, LeftAlt,
		F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
		Count
	};

	enum class MouseButton : u8 { Left, Right, Middle, X1, X2, Count };
	enum class MouseAxis : u8 { DeltaX, DeltaY, ScrollX, ScrollY, Count };

	enum class PadButton : u8
	{
		A, B, X, Y, LeftBumper, RightBumper, Back, Start, Guide, LeftThumb, RightThumb,
		DPadUp, DPadRight, DPadDown, DPadLeft,
		Count
	};

	enum class PadAxis : u8 { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger, Count };

	constexpr u32 KEY_COUNT = static_cast<u32>(Key::Count);
	constexpr u32 MOUSE_BUTTON_COUNT = static_cast<u32>(MouseButton::Count);
	constexpr u32 MOUSE_AXIS_COUNT = static_cast<u32>(MouseAxis::Count);
	constexpr u32 PAD_BUTTON_COUNT = static_cast<u32>(PadButton::Count);
	constexpr u32 PAD_AXIS_COUNT = static_cast<u32>(PadAxis::Count);

	// A mouse axis is stored in InputControl::Code as MOUSE_AXIS_CODE_BASE + axis, so it cannot be confused with a button.
	// A pad axis likewise as PAD_BUTTON_COUNT + axis.
	constexpr u16 MOUSE_AXIS_CODE_BASE = 16;

	// One control: Key.W, Mouse.Left, Mouse.DeltaX, Pad.LeftX (the names used by files and Lua).
	struct InputControl
	{
		InputDevice Device = InputDevice::Key;
		u16 Code = 0;

		bool operator==(const InputControl&) const = default;
	};

	inline InputControl control(Key key) { return { InputDevice::Key, static_cast<u16>(key) }; }
	inline InputControl control(MouseButton button) { return { InputDevice::Mouse, static_cast<u16>(button) }; }
	inline InputControl control(MouseAxis axis) { return { InputDevice::Mouse, static_cast<u16>(MOUSE_AXIS_CODE_BASE + static_cast<u16>(axis)) }; }
	inline InputControl control(PadButton button) { return { InputDevice::Pad, static_cast<u16>(button) }; }
	inline InputControl control(PadAxis axis) { return { InputDevice::Pad, static_cast<u16>(PAD_BUTTON_COUNT + static_cast<u16>(axis)) }; }

	// True for the mouse axes and the pad axes (sticks and triggers); false for every button-like control.
	bool control_is_axis(const InputControl& control);

	std::string control_name(const InputControl& control);
	std::optional<InputControl> parse_control(std::string_view name);

	// Every valid control, in a stable order.
	const std::vector<InputControl>& all_controls();
}
