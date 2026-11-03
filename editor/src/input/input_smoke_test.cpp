#include <editor_common.h>
#include "input_smoke_test.h"

#include <core/engine.h>
#include <input/input_codes.h>
#include <input/input_module.h>
#include <input/input_state.h>
#include <scripting/script_module.h>
#include <ui/ui_module.h>
#include <input/scripted_input_backend.h>

#define WI_INPUT_LOG(level, ...) WI_LOG(level, "Input", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		struct Check
		{
			u32 Failures = 0;

			void operator()(bool condition, const std::string& what)
			{
				if (!condition)
				{
					Failures++;
					WI_INPUT_LOG(Error, "Input smoke test FAILED: {0}", what);
				}
			}
		};

		bool near_equal(float a, float b)
		{
			return std::abs(a - b) < 1e-5f;
		}

		void raw_input(Check& check)
		{
			const InputControl keyW = control(Key::W);
			const InputControl mouseLeft = control(MouseButton::Left);
			const InputControl padA = control(PadButton::A);
			const InputControl padLeftX = control(PadAxis::LeftX);

			{
				ScriptedInputBackend backend;
				InputState state;

				backend.key(Key::W, true);
				state.update(backend);
				check(state.down(keyW) && state.pressed(keyW) && !state.released(keyW), "raw_edges: first frame down gives down + pressed");
				state.update(backend);
				check(state.down(keyW) && !state.pressed(keyW), "raw_edges: a held key is down without a new press");
				backend.key(Key::W, false);
				state.update(backend);
				check(!state.down(keyW) && state.released(keyW) && !state.pressed(keyW), "raw_edges: release gives released, not down");
				state.update(backend);
				check(!state.released(keyW), "raw_edges: the release edge lasts one frame");
			}

			{
				ScriptedInputBackend backend;
				InputState state;

				backend.key(Key::W, true);
				backend.key(Key::W, false);
				state.update(backend);
				check(state.pressed(keyW) && state.released(keyW) && !state.down(keyW), "raw_tap: press and release inside one frame gives both edges and is not down");
			}

			{
				ScriptedInputBackend backend;
				InputState state;

				backend.move_cursor({ 10.0f, 20.0f });
				state.update(backend);
				backend.move_cursor({ 15.0f, 18.0f });
				backend.scroll({ 0.0f, 2.0f });
				state.update(backend);
				check(near_equal(state.mouse_delta().x, 5.0f) && near_equal(state.mouse_delta().y, -2.0f), "raw_mouse: delta is the cursor change between frames");
				check(near_equal(state.mouse_position().x, 15.0f), "raw_mouse: position");
				check(near_equal(state.scroll().y, 2.0f) && near_equal(state.value(control(MouseAxis::ScrollY)), 2.0f), "raw_mouse: scroll is reported");
				state.update(backend);
				check(near_equal(state.scroll().y, 0.0f) && near_equal(state.mouse_delta().x, 0.0f), "raw_mouse: scroll and delta are cleared the next frame");
			}

			{
				ScriptedInputBackend backend;
				InputState state;

				backend.connect_pad(0, true);
				backend.pad_axis(0, PadAxis::LeftX, 0.5f);
				state.update(backend);
				check(state.pad_connected(0) && !state.pad_connected(1), "raw_pad: connection");
				check(near_equal(state.value(padLeftX), 0.5f), "raw_pad: axis value");

				backend.connect_pad(1, true);
				backend.pad_axis(1, PadAxis::LeftX, -0.9f);
				state.update(backend);
				check(near_equal(state.value(padLeftX), -0.9f), "raw_pad: with two pads the largest magnitude wins");

				backend.pad_button(1, PadButton::A, true);
				state.update(backend);
				check(state.down(padA) && state.pressed(padA), "raw_pad: a button on any pad counts");
			}

			{
				ScriptedInputBackend backend;
				InputState state;

				backend.connect_pad(0, true);
				backend.pad_button(0, PadButton::A, true);
				state.update(backend);
				backend.connect_pad(0, false);
				state.update(backend);
				check(!state.down(padA) && state.released(padA), "raw_pad_unplug: a held button is released, not stuck");
				state.update(backend);
				check(!state.down(padA) && !state.released(padA), "raw_pad_unplug: stays up");
			}

			{
				ScriptedInputBackend backend;
				InputState state;

				backend.key(Key::W, true);
				backend.key(Key::Space, true);
				backend.mouse_button(MouseButton::Left, true);
				state.update(backend);
				backend.focus_lost();
				state.update(backend);
				check(!state.down(keyW) && state.released(keyW), "raw_focus_lost: key released with an edge");
				check(!state.down(control(Key::Space)) && state.released(control(Key::Space)), "raw_focus_lost: second key released");
				check(!state.down(mouseLeft) && state.released(mouseLeft), "raw_focus_lost: mouse button released with an edge");
				state.update(backend);
				check(!state.down(keyW) && !state.down(mouseLeft), "raw_focus_lost: nothing comes back");
			}

			{
				ScriptedInputBackend backend;
				InputState state;

				backend.key(Key::W, true);
				backend.mouse_button(MouseButton::Left, true);
				backend.move_cursor({ 5.0f, 5.0f });
				backend.connect_pad(0, true);
				backend.pad_axis(0, PadAxis::LeftX, 0.7f);
				backend.pad_button(0, PadButton::A, true);
				state.set_capture(false);
				state.update(backend);
				check(!state.down(keyW) && !state.pressed(keyW) && !state.down(mouseLeft), "raw_capture_gate: key and mouse report nothing");
				check(near_equal(state.mouse_position().x, 0.0f) && near_equal(state.mouse_delta().x, 0.0f), "raw_capture_gate: cursor reports nothing");
				check(state.down(padA) && near_equal(state.value(padLeftX), 0.7f), "raw_capture_gate: the pad still works");
				state.set_capture(true);
				check(state.down(keyW) && state.down(mouseLeft), "raw_capture_gate: capture back on shows the held state");
			}

			{
				bool allRoundTrip = true;
				for (const InputControl& c : all_controls())
				{
					const auto parsed = parse_control(control_name(c));
					allRoundTrip = allRoundTrip && parsed.has_value() && *parsed == c;
				}
				check(allRoundTrip && !all_controls().empty(), "raw_names: parse_control(control_name(c)) == c for every control");
				check(!parse_control("Key.Nope").has_value() && !parse_control("Foo.A").has_value() && !parse_control("Key").has_value() && !parse_control("").has_value(),
					"raw_names: unknown names are empty");
				check(control_name(keyW) == "Key.W" && control_name(control(MouseAxis::DeltaX)) == "Mouse.DeltaX" && control_name(padLeftX) == "Pad.LeftX",
					"raw_names: spelling");
				check(control_is_axis(padLeftX) && control_is_axis(control(MouseAxis::DeltaY)) && !control_is_axis(keyW) && !control_is_axis(mouseLeft) && !control_is_axis(padA),
					"raw_names: axis classification");
			}
		}

		void input_module(Check& check)
		{
			Engine& engine = Engine::get();
			InputModule* module = engine.get_module<InputModule>();
			check(module != nullptr, "module_registered: the engine has an InputModule");

			UiModule* ui = engine.get_module<UiModule>();
			ScriptingModule* scripting = engine.get_module<ScriptingModule>();
			InputModule probe;
			// Modules update in ascending priority order (ModuleManager::begin sorts with <), so a lower value runs first.
			check(ui && scripting && probe.get_priority() < ui->get_priority() && probe.get_priority() < scripting->get_priority(),
				"module_priority: input updates before the UI and the scripts");

			// A separate module instance: the engine's own one is updated by the main loop at the same time.
			InputModule local;
			ScriptedInputBackend scripted;
			check(local.backend() == nullptr, "module_scripted_swap: no backend without init");
			local.set_backend(&scripted);
			scripted.key(Key::W, true);
			local.update(0.0f);
			check(local.state().down(control(Key::W)) && local.state().pressed(control(Key::W)), "module_scripted_swap: the injected backend feeds the state");
			local.set_backend(nullptr);
			check(local.backend() == nullptr, "module_scripted_swap: null restores the default backend");

			local.set_backend(&scripted);
			local.set_cursor_mode(CursorMode::Locked);
			check(scripted.cursor_mode() == CursorMode::Locked, "module_cursor_mode: reaches the backend");
			local.set_capture(false);
			local.update(0.0f);
			check(!local.state().down(control(Key::W)), "module_capture: capture off hides the keyboard");
		}
	}

	bool run_input_smoke_test(AssetPipelineModule& pipeline)
	{
		(void)pipeline;

		Check check;
		raw_input(check);
		input_module(check);

		if (check.Failures == 0)
		{
			WI_INPUT_LOG(Info, "Input smoke test passed");
		}
		else
		{
			WI_INPUT_LOG(Error, "Input smoke test FAILED: {0} check(s)", check.Failures);
		}
		return check.Failures == 0;
	}
}
