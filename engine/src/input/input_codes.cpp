#include <pch.h>
#include "input_codes.h"

namespace wiwa
{
	namespace
	{
		constexpr const char* KEY_NAMES[KEY_COUNT] =
		{
			"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
			"0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
			"Space", "Enter", "Escape", "Tab", "Backspace",
			"Left", "Right", "Up", "Down",
			"LeftShift", "LeftCtrl", "LeftAlt",
			"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12"
		};

		constexpr const char* MOUSE_BUTTON_NAMES[MOUSE_BUTTON_COUNT] = { "Left", "Right", "Middle", "X1", "X2" };
		constexpr const char* MOUSE_AXIS_NAMES[MOUSE_AXIS_COUNT] = { "DeltaX", "DeltaY", "ScrollX", "ScrollY" };

		constexpr const char* PAD_BUTTON_NAMES[PAD_BUTTON_COUNT] =
		{
			"A", "B", "X", "Y", "LeftBumper", "RightBumper", "Back", "Start", "Guide", "LeftThumb", "RightThumb",
			"DPadUp", "DPadRight", "DPadDown", "DPadLeft"
		};
		constexpr const char* PAD_AXIS_NAMES[PAD_AXIS_COUNT] = { "LeftX", "LeftY", "RightX", "RightY", "LeftTrigger", "RightTrigger" };
	}

	bool control_is_axis(const InputControl& control)
	{
		switch (control.Device)
		{
		case InputDevice::Mouse: return control.Code >= MOUSE_AXIS_CODE_BASE;
		case InputDevice::Pad: return control.Code >= PAD_BUTTON_COUNT;
		default: return false;
		}
	}

	std::string control_name(const InputControl& control)
	{
		switch (control.Device)
		{
		case InputDevice::Key:
			if (control.Code < KEY_COUNT) return std::string("Key.") + KEY_NAMES[control.Code];
			break;
		case InputDevice::Mouse:
			if (control.Code < MOUSE_BUTTON_COUNT) return std::string("Mouse.") + MOUSE_BUTTON_NAMES[control.Code];
			if (control.Code >= MOUSE_AXIS_CODE_BASE && control.Code - MOUSE_AXIS_CODE_BASE < MOUSE_AXIS_COUNT)
				return std::string("Mouse.") + MOUSE_AXIS_NAMES[control.Code - MOUSE_AXIS_CODE_BASE];
			break;
		case InputDevice::Pad:
			if (control.Code < PAD_BUTTON_COUNT) return std::string("Pad.") + PAD_BUTTON_NAMES[control.Code];
			if (control.Code >= PAD_BUTTON_COUNT && control.Code - PAD_BUTTON_COUNT < PAD_AXIS_COUNT)
				return std::string("Pad.") + PAD_AXIS_NAMES[control.Code - PAD_BUTTON_COUNT];
			break;
		}
		return {};
	}

	const std::vector<InputControl>& all_controls()
	{
		static const std::vector<InputControl> controls = []
		{
			std::vector<InputControl> list;
			for (u32 i = 0; i < KEY_COUNT; i++) list.push_back(control(static_cast<Key>(i)));
			for (u32 i = 0; i < MOUSE_BUTTON_COUNT; i++) list.push_back(control(static_cast<MouseButton>(i)));
			for (u32 i = 0; i < MOUSE_AXIS_COUNT; i++) list.push_back(control(static_cast<MouseAxis>(i)));
			for (u32 i = 0; i < PAD_BUTTON_COUNT; i++) list.push_back(control(static_cast<PadButton>(i)));
			for (u32 i = 0; i < PAD_AXIS_COUNT; i++) list.push_back(control(static_cast<PadAxis>(i)));
			return list;
		}();
		return controls;
	}

	std::optional<InputControl> parse_control(std::string_view name)
	{
		for (const InputControl& candidate : all_controls())
		{
			if (control_name(candidate) == name)
			{
				return candidate;
			}
		}
		return std::nullopt;
	}
}
