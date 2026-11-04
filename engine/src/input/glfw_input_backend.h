#pragma once

#include "input_backend.h"
#include "scripted_input_backend.h"

struct GLFWwindow;

namespace wiwa
{
	// Keyboard, mouse and gamepad input from GLFW. The window callbacks are chained: each one runs ours and then the callback that
	// was installed before (the ImGui GLFW backend, in either install order). Pads are polled every frame.
	// There is a single instance (the GLFW callbacks carry no user data).
	class GlfwInputBackend final : public IInputBackend
	{
	public:
		explicit GlfwInputBackend(GLFWwindow* window);
		virtual ~GlfwInputBackend() override;

		virtual void begin_frame(RawInputState& out) override;
		virtual void set_cursor_mode(CursorMode mode) override;

	private:
		friend struct GlfwInputCallbacks;

		void poll_pads();

		GLFWwindow* m_Window = nullptr;
		// Accumulates the events with the same edge rules as the scripted backend (a tap shorter than a frame keeps both edges).
		ScriptedInputBackend m_Events;

		void* m_PrevKey = nullptr;
		void* m_PrevMouseButton = nullptr;
		void* m_PrevCursorPos = nullptr;
		void* m_PrevScroll = nullptr;
		void* m_PrevFocus = nullptr;
	};
}
