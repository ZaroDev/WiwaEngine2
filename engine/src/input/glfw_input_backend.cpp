#include <pch.h>
#include "glfw_input_backend.h"

#include <GLFW/glfw3.h>

namespace wiwa
{
	namespace
	{
		GlfwInputBackend* s_Instance = nullptr;

		bool key_from_glfw(int glfwKey, Key& out)
		{
			if (glfwKey >= GLFW_KEY_A && glfwKey <= GLFW_KEY_Z) { out = static_cast<Key>(static_cast<int>(Key::A) + glfwKey - GLFW_KEY_A); return true; }
			if (glfwKey >= GLFW_KEY_0 && glfwKey <= GLFW_KEY_9) { out = static_cast<Key>(static_cast<int>(Key::Num0) + glfwKey - GLFW_KEY_0); return true; }
			if (glfwKey >= GLFW_KEY_F1 && glfwKey <= GLFW_KEY_F12) { out = static_cast<Key>(static_cast<int>(Key::F1) + glfwKey - GLFW_KEY_F1); return true; }

			switch (glfwKey)
			{
			case GLFW_KEY_SPACE: out = Key::Space; return true;
			case GLFW_KEY_ENTER: out = Key::Enter; return true;
			case GLFW_KEY_ESCAPE: out = Key::Escape; return true;
			case GLFW_KEY_TAB: out = Key::Tab; return true;
			case GLFW_KEY_BACKSPACE: out = Key::Backspace; return true;
			case GLFW_KEY_LEFT: out = Key::Left; return true;
			case GLFW_KEY_RIGHT: out = Key::Right; return true;
			case GLFW_KEY_UP: out = Key::Up; return true;
			case GLFW_KEY_DOWN: out = Key::Down; return true;
			case GLFW_KEY_LEFT_SHIFT: out = Key::LeftShift; return true;
			case GLFW_KEY_LEFT_CONTROL: out = Key::LeftCtrl; return true;
			case GLFW_KEY_LEFT_ALT: out = Key::LeftAlt; return true;
			default: return false;
			}
		}

		bool mouse_button_from_glfw(int button, MouseButton& out)
		{
			switch (button)
			{
			case GLFW_MOUSE_BUTTON_LEFT: out = MouseButton::Left; return true;
			case GLFW_MOUSE_BUTTON_RIGHT: out = MouseButton::Right; return true;
			case GLFW_MOUSE_BUTTON_MIDDLE: out = MouseButton::Middle; return true;
			case GLFW_MOUSE_BUTTON_4: out = MouseButton::X1; return true;
			case GLFW_MOUSE_BUTTON_5: out = MouseButton::X2; return true;
			default: return false;
			}
		}
	}

	// The callbacks call the handler of the instance (if it still exists) and then the one that was installed before.
	struct GlfwInputCallbacks
	{
		static void key(GLFWwindow* window, int glfwKey, int scancode, int action, int mods)
		{
			Key key;
			if (s_Instance && action != GLFW_REPEAT && key_from_glfw(glfwKey, key))
			{
				s_Instance->m_Events.key(key, action == GLFW_PRESS);
			}
			if (s_Instance && s_Instance->m_PrevKey)
			{
				reinterpret_cast<GLFWkeyfun>(s_Instance->m_PrevKey)(window, glfwKey, scancode, action, mods);
			}
		}

		static void mouse_button(GLFWwindow* window, int button, int action, int mods)
		{
			MouseButton mouseButton;
			if (s_Instance && mouse_button_from_glfw(button, mouseButton))
			{
				s_Instance->m_Events.mouse_button(mouseButton, action == GLFW_PRESS);
			}
			if (s_Instance && s_Instance->m_PrevMouseButton)
			{
				reinterpret_cast<GLFWmousebuttonfun>(s_Instance->m_PrevMouseButton)(window, button, action, mods);
			}
		}

		static void cursor_pos(GLFWwindow* window, double x, double y)
		{
			if (s_Instance)
			{
				s_Instance->m_Events.move_cursor({ static_cast<float>(x), static_cast<float>(y) });
			}
			if (s_Instance && s_Instance->m_PrevCursorPos)
			{
				reinterpret_cast<GLFWcursorposfun>(s_Instance->m_PrevCursorPos)(window, x, y);
			}
		}

		static void scroll(GLFWwindow* window, double x, double y)
		{
			if (s_Instance)
			{
				s_Instance->m_Events.scroll({ static_cast<float>(x), static_cast<float>(y) });
			}
			if (s_Instance && s_Instance->m_PrevScroll)
			{
				reinterpret_cast<GLFWscrollfun>(s_Instance->m_PrevScroll)(window, x, y);
			}
		}

		static void focus(GLFWwindow* window, int focused)
		{
			if (s_Instance && !focused)
			{
				// Alt-tab with keys held: nothing stays stuck down.
				s_Instance->m_Events.focus_lost();
			}
			if (s_Instance && s_Instance->m_PrevFocus)
			{
				reinterpret_cast<GLFWwindowfocusfun>(s_Instance->m_PrevFocus)(window, focused);
			}
		}
	};

	GlfwInputBackend::GlfwInputBackend(GLFWwindow* window) : m_Window(window)
	{
		s_Instance = this;

		m_PrevKey = reinterpret_cast<void*>(glfwSetKeyCallback(window, GlfwInputCallbacks::key));
		m_PrevMouseButton = reinterpret_cast<void*>(glfwSetMouseButtonCallback(window, GlfwInputCallbacks::mouse_button));
		m_PrevCursorPos = reinterpret_cast<void*>(glfwSetCursorPosCallback(window, GlfwInputCallbacks::cursor_pos));
		m_PrevScroll = reinterpret_cast<void*>(glfwSetScrollCallback(window, GlfwInputCallbacks::scroll));
		m_PrevFocus = reinterpret_cast<void*>(glfwSetWindowFocusCallback(window, GlfwInputCallbacks::focus));

		double x = 0.0, y = 0.0;
		glfwGetCursorPos(window, &x, &y);
		m_Events.move_cursor({ static_cast<float>(x), static_cast<float>(y) });
	}

	GlfwInputBackend::~GlfwInputBackend()
	{
		// Put the previous callback back unless something was installed on top of ours; that one keeps calling through our static
		// functions, which only forward once the instance is gone.
		auto restore = [this](auto previous, auto ours, auto setter)
		{
			auto current = setter(m_Window, reinterpret_cast<decltype(ours)>(previous));
			if (current != ours)
			{
				setter(m_Window, current);
			}
		};

		restore(m_PrevKey, GlfwInputCallbacks::key, glfwSetKeyCallback);
		restore(m_PrevMouseButton, GlfwInputCallbacks::mouse_button, glfwSetMouseButtonCallback);
		restore(m_PrevCursorPos, GlfwInputCallbacks::cursor_pos, glfwSetCursorPosCallback);
		restore(m_PrevScroll, GlfwInputCallbacks::scroll, glfwSetScrollCallback);
		restore(m_PrevFocus, GlfwInputCallbacks::focus, glfwSetWindowFocusCallback);

		s_Instance = nullptr;
	}

	void GlfwInputBackend::poll_pads()
	{
		for (u32 pad = 0; pad < MAX_GAMEPADS; pad++)
		{
			GLFWgamepadstate state;
			const int jid = GLFW_JOYSTICK_1 + static_cast<int>(pad);
			if (!glfwJoystickIsGamepad(jid) || !glfwGetGamepadState(jid, &state))
			{
				m_Events.connect_pad(pad, false);
				continue;
			}

			m_Events.connect_pad(pad, true);

			// Our PadButton order is GLFW's (A, B, X, Y, bumpers, back, start, guide, thumbs, dpad up/right/down/left).
			for (u32 button = 0; button < PAD_BUTTON_COUNT; button++)
			{
				m_Events.pad_button(pad, static_cast<PadButton>(button), state.buttons[button] == GLFW_PRESS);
			}

			// GLFW has +Y down on the sticks; up is +1 for us. Triggers come as [-1, 1] and go to [0, 1].
			m_Events.pad_axis(pad, PadAxis::LeftX, state.axes[GLFW_GAMEPAD_AXIS_LEFT_X]);
			m_Events.pad_axis(pad, PadAxis::LeftY, -state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y]);
			m_Events.pad_axis(pad, PadAxis::RightX, state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X]);
			m_Events.pad_axis(pad, PadAxis::RightY, -state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y]);
			m_Events.pad_axis(pad, PadAxis::LeftTrigger, (state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] + 1.0f) * 0.5f);
			m_Events.pad_axis(pad, PadAxis::RightTrigger, (state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1.0f) * 0.5f);
		}
	}

	void GlfwInputBackend::begin_frame(RawInputState& out)
	{
		poll_pads();
		m_Events.begin_frame(out);
	}

	void GlfwInputBackend::set_cursor_mode(CursorMode mode)
	{
		int glfwMode = GLFW_CURSOR_NORMAL;
		if (mode == CursorMode::Hidden) glfwMode = GLFW_CURSOR_HIDDEN;
		if (mode == CursorMode::Locked) glfwMode = GLFW_CURSOR_DISABLED;
		glfwSetInputMode(m_Window, GLFW_CURSOR, glfwMode);
	}
}
