#include <pch.h>
#include "input_ui_source.h"

#include "input_module.h"

namespace wiwa
{
	namespace
	{
		Binding control_binding(const InputControl& control, bool invert = false)
		{
			Binding binding;
			binding.BindingKind = Binding::Kind::Control;
			binding.Parts[0] = control;
			binding.Invert = invert;
			return binding;
		}

		struct NavEntry
		{
			const char* Action;
			UiNav Nav;
		};

		constexpr NavEntry NAV_ACTIONS[] =
		{
			{ "NavUp", UiNav::Up },
			{ "NavDown", UiNav::Down },
			{ "NavLeft", UiNav::Left },
			{ "NavRight", UiNav::Right },
			{ "Submit", UiNav::Submit },
			{ "Cancel", UiNav::Cancel },
		};
	}

	ActionMap ui_nav_action_map()
	{
		ActionMap map;
		map.Name = UI_NAV_MAP;
		// The stick presses in the direction it is pushed (Y is up-positive), so down and left bind it inverted.
		map.Actions.push_back({ "NavUp", ActionType::Button, { control_binding(control(Key::Up)), control_binding(control(PadButton::DPadUp)), control_binding(control(PadAxis::LeftY)) } });
		map.Actions.push_back({ "NavDown", ActionType::Button, { control_binding(control(Key::Down)), control_binding(control(PadButton::DPadDown)), control_binding(control(PadAxis::LeftY), true) } });
		map.Actions.push_back({ "NavLeft", ActionType::Button, { control_binding(control(Key::Left)), control_binding(control(PadButton::DPadLeft)), control_binding(control(PadAxis::LeftX), true) } });
		map.Actions.push_back({ "NavRight", ActionType::Button, { control_binding(control(Key::Right)), control_binding(control(PadButton::DPadRight)), control_binding(control(PadAxis::LeftX)) } });
		map.Actions.push_back({ "Submit", ActionType::Button, { control_binding(control(Key::Enter)), control_binding(control(PadButton::A)) } });
		map.Actions.push_back({ "Cancel", ActionType::Button, { control_binding(control(Key::Escape)), control_binding(control(PadButton::B)) } });
		return map;
	}

	void InputUiSource::set_viewport(const glm::vec2& origin, float scale, bool hovered)
	{
		m_Origin = origin;
		m_Scale = scale > 0.0f ? scale : 1.0f;
		m_Hovered = hovered;
	}

	UiInputFrame InputUiSource::poll()
	{
		const InputState& input = m_Module.state();
		UiInputFrame frame;

		// The InputState reads nothing for the mouse while the capture is off.
		if (input.capture() && m_Hovered)
		{
			frame.Pointer = (input.mouse_position() - m_Origin) * m_Scale;
			frame.PointerValid = true;
		}

		constexpr MouseButton buttons[UI_POINTER_BUTTONS] = { MouseButton::Left, MouseButton::Right, MouseButton::Middle };
		for (u32 button = 0; button < UI_POINTER_BUTTONS; button++)
		{
			const InputControl mouse = control(buttons[button]);
			frame.Down[button] = input.down(mouse);
			frame.Pressed[button] = input.pressed(mouse);
			frame.Released[button] = input.released(mouse);
		}
		frame.Scroll = input.scroll().y;

		const ActionSystem& actions = m_Module.actions();
		for (const NavEntry& entry : NAV_ACTIONS)
		{
			if (actions.state(UI_NAV_MAP, entry.Action).Pressed)
			{
				frame.NavPressed |= ui_nav_bit(entry.Nav);
			}
		}
		return frame;
	}
}
