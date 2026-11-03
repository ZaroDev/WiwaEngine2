#pragma once

#include <core/module.h>

#include "input_backend.h"
#include "input_state.h"

namespace wiwa
{
	// Keyboard, mouse and gamepad input for the game and the editor. Main thread only. It reads its raw state from an
	// IInputBackend (GLFW from the window, or one injected with set_backend) once per frame, before the UI and the scripts.
	class InputModule final : public IModule
	{
	public:
		InputModule() = default;
		virtual ~InputModule() override = default;

		virtual void init() override;
		virtual void shutdown() override;
		virtual void update(float deltaTime) override;

		virtual UpdateFlags get_update_flags() const override { return UpdateFlags::UPDATE; }
		virtual std::string get_name() const override { return "InputModule"; }
		virtual int get_priority() const override { return -30; } // after the scene (-80) and the renderer (-50), before the UI (-10) and the scripts (0)

		InputState& state() { return m_State; }
		const InputState& state() const { return m_State; }

		// Replaces the source of raw input (not owned; it must outlive its use). Null goes back to the GLFW backend, if there is one.
		void set_backend(IInputBackend* backend);
		IInputBackend* backend() const { return m_External ? m_External : m_Glfw.get(); }

		void set_cursor_mode(CursorMode mode);
		CursorMode cursor_mode() const { return m_CursorMode; }
		void set_capture(bool keyboardMouse) { m_State.set_capture(keyboardMouse); }

	private:
		InputState m_State;
		Scope<IInputBackend> m_Glfw;
		IInputBackend* m_External = nullptr;
		CursorMode m_CursorMode = CursorMode::Normal;
	};
}
