#include <pch.h>
#include "input_module.h"

#include "glfw_input_backend.h"

#include <core/engine.h>
#include <renderer/window.h>

namespace wiwa
{
	void InputModule::init()
	{
		// The window module is added before this one. Without a window (nothing to listen to) the module still works with set_backend.
		Window* window = Engine::get().get_module<Window>();
		if (window && window->get_window())
		{
			m_Glfw = CreateScope<GlfwInputBackend>(window->get_window());
		}
	}

	void InputModule::shutdown()
	{
		m_External = nullptr;
		m_Glfw.reset();
	}

	void InputModule::update(float deltaTime)
	{
		if (IInputBackend* source = backend())
		{
			m_State.update(*source);
		}
	}

	void InputModule::set_backend(IInputBackend* backend)
	{
		m_External = backend;
		if (IInputBackend* source = this->backend())
		{
			source->set_cursor_mode(m_CursorMode);
		}
	}

	void InputModule::set_cursor_mode(CursorMode mode)
	{
		m_CursorMode = mode;
		if (IInputBackend* source = backend())
		{
			source->set_cursor_mode(mode);
		}
	}
}
