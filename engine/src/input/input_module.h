#pragma once

#include <filesystem>

#include <assets/handle.h>
#include <core/module.h>

#include "action_system.h"
#include "input_backend.h"
#include "input_state.h"
#include "input_ui_source.h"

namespace wiwa
{
	// Keyboard, mouse and gamepad input for the game and the editor. Main thread only. It reads its raw state from an
	// IInputBackend (GLFW from the window, or one injected with set_backend) once per frame, before the UI and the scripts, and
	// evaluates the action maps from it.
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
		virtual int get_priority() const override { return ModulePriority::Input; }

		InputState& state() { return m_State; }
		const InputState& state() const { return m_State; }
		ActionSystem& actions() { return m_Actions; }
		const ActionSystem& actions() const { return m_Actions; }

		// Replaces the source of raw input (not owned; it must outlive its use). Null goes back to the GLFW backend, if there is one.
		void set_backend(IInputBackend* backend);
		IInputBackend* backend() const { return m_External ? m_External : m_Glfw.get(); }
		// True while a backend was injected with set_backend (tests and tools drive the input then): the editor leaves the capture to them.
		bool backend_injected() const { return m_External != nullptr; }

		void set_cursor_mode(CursorMode mode);
		CursorMode cursor_mode() const { return m_CursorMode; }
		void set_capture(bool keyboardMouse) { m_State.set_capture(keyboardMouse); }

		// The game UI reads its pointer and navigation from here (UiModule::set_input_source). The game sets it at start-up; the editor while
		// a scene plays.
		InputUiSource& ui_source() { return m_UiSource; }

		// Registers the "UI" navigation map (ui_nav_action_map) and puts it on the stack. init() does this; it is below any map pushed later.
		void add_ui_nav_map();

		// Registers maps made in code (they are defaults like the asset's); they survive the default asset arriving.
		void add_maps(std::vector<ActionMap> maps);

		// The .winput asset with the default maps. It loads in the background; when it is ready the maps are replaced (code-registered
		// ones stay) and the override file is applied on top. A failed load logs one warning and leaves the maps as they are.
		void set_default_map(const AssetRef& ref);

		// The player's rebinds (see input_overrides.h). The path defaults to input_overrides.json in the working directory.
		void set_overrides_path(std::filesystem::path path) { m_OverridesPath = std::move(path); }
		// Writes the rebinds. False if the file could not be written.
		bool save_overrides();
		// Reads and applies the file; stale entries are skipped with a warning. False if the file is missing or is not usable at all,
		// in which case the bindings stay as they are.
		bool load_overrides();

	private:
		void apply_pending();

		InputState m_State;
		ActionSystem m_Actions;
		InputUiSource m_UiSource{ *this };
		Scope<IInputBackend> m_Glfw;
		IInputBackend* m_External = nullptr;
		CursorMode m_CursorMode = CursorMode::Normal;

		std::vector<ActionMap> m_Registered;
		Handle<InputMapData> m_DefaultMap;
		bool m_DefaultPending = false;
		bool m_OverridesPending = false;
		std::filesystem::path m_OverridesPath = "input_overrides.json";
	};
}
