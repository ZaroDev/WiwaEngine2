#include <editor_common.h>
#include "input_smoke_test.h"

#include <core/engine.h>
#include <input/action_system.h>
#include <input/action_types.h>
#include <input/input_codes.h>
#include <input/input_map_io.h>
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

		Binding control_binding(Key key, float scale = 1.0f, float deadzone = 0.0f, bool invert = false)
		{
			Binding binding;
			binding.BindingKind = Binding::Kind::Control;
			binding.Parts[0] = control(key);
			binding.Scale = scale;
			binding.Deadzone = deadzone;
			binding.Invert = invert;
			return binding;
		}

		Binding composite1d(Key negative, Key positive)
		{
			Binding binding;
			binding.BindingKind = Binding::Kind::Composite1D;
			binding.Parts[0] = control(negative);
			binding.Parts[1] = control(positive);
			return binding;
		}

		Binding composite2d(Key up, Key down, Key left, Key right)
		{
			Binding binding;
			binding.BindingKind = Binding::Kind::Composite2D;
			binding.Parts[0] = control(up);
			binding.Parts[1] = control(down);
			binding.Parts[2] = control(left);
			binding.Parts[3] = control(right);
			return binding;
		}

		Binding pair2d(PadAxis x, PadAxis y, float deadzone)
		{
			Binding binding;
			binding.BindingKind = Binding::Kind::Pair2D;
			binding.Parts[0] = control(x);
			binding.Parts[1] = control(y);
			binding.Deadzone = deadzone;
			return binding;
		}

		InputMapData sample_map_data()
		{
			InputMapData data;

			ActionMap gameplay;
			gameplay.Name = "Gameplay";
			gameplay.Actions.push_back({ "Jump", ActionType::Button, { control_binding(Key::Space), control_binding(Key::Enter, 1.0f, 0.0f, true) } });
			gameplay.Actions.push_back({ "Throttle", ActionType::Axis1D, { composite1d(Key::S, Key::W), control_binding(Key::Up, 0.5f, 0.1f) } });
			gameplay.Actions.push_back({ "Move", ActionType::Axis2D, { composite2d(Key::W, Key::S, Key::A, Key::D), pair2d(PadAxis::LeftX, PadAxis::LeftY, 0.2f) } });
			data.Maps.push_back(gameplay);

			ActionMap ui;
			ui.Name = "UI";
			ui.Consume = true;
			ui.Actions.push_back({ "Submit", ActionType::Button, { control_binding(Key::Enter) } });
			data.Maps.push_back(ui);
			return data;
		}

		bool same_map_data(const InputMapData& a, const InputMapData& b)
		{
			return input_map_to_text(a) == input_map_to_text(b);
		}

		void input_map_format(Check& check)
		{
			const InputMapData sample = sample_map_data();
			std::string error;
			check(input_map_valid(sample, error) && error.empty(), "map_roundtrip: the sample map is valid");

			const std::string text = input_map_to_text(sample);
			InputMapData parsed;
			check(input_map_from_text(text, parsed, error), "map_roundtrip: the written text parses (" + error + ")");
			check(parsed.Maps.size() == 2 && parsed.Maps[0].Actions.size() == 3 && parsed.Maps[1].Consume, "map_roundtrip: structure survives");
			check(parsed.Maps[0].Actions[2].Bindings.size() == 2 && parsed.Maps[0].Actions[2].Bindings[1].BindingKind == Binding::Kind::Pair2D &&
				near_equal(parsed.Maps[0].Actions[2].Bindings[1].Deadzone, 0.2f), "map_roundtrip: binding kinds and values survive");
			check(input_map_to_text(parsed) == text, "map_roundtrip: text -> data -> text is byte-identical");

			// Omitted optional keys take their defaults.
			const char* defaults = R"({"version":1,"maps":[{"name":"M","actions":[{"name":"A","type":"Button","bindings":[{"control":"Key.Space"}]}]}]})";
			InputMapData defaulted;
			check(input_map_from_text(defaults, defaulted, error), "map_defaults: minimal file parses (" + error + ")");
			if (!defaulted.Maps.empty() && !defaulted.Maps[0].Actions.empty() && !defaulted.Maps[0].Actions[0].Bindings.empty())
			{
				const Binding& binding = defaulted.Maps[0].Actions[0].Bindings[0];
				check(near_equal(binding.Scale, 1.0f) && near_equal(binding.Deadzone, 0.0f) && !binding.Invert && !defaulted.Maps[0].Consume,
					"map_defaults: scale 1, deadzone 0, no invert, no consume");
			}
			else
			{
				check(false, "map_defaults: the parsed map has its action and binding");
			}

			struct Reject { const char* Name; const char* Text; const char* Offender; };
			const Reject rejects[] =
			{
				{ "not json", "this is not json", "" },
				{ "version 2", R"({"version":2,"maps":[]})", "version" },
				{ "unknown control", R"({"version":1,"maps":[{"name":"M","actions":[{"name":"A","type":"Button","bindings":[{"control":"Key.Nope"}]}]}]})", "Key.Nope" },
				{ "button with composite", R"({"version":1,"maps":[{"name":"M","actions":[{"name":"Jmp","type":"Button","bindings":[{"negative":"Key.A","positive":"Key.D"}]}]}]})", "Jmp" },
				{ "axis2d with control", R"({"version":1,"maps":[{"name":"M","actions":[{"name":"Mv","type":"Axis2D","bindings":[{"control":"Key.A"}]}]}]})", "Mv" },
				{ "duplicate action", R"({"version":1,"maps":[{"name":"M","actions":[{"name":"A","type":"Button","bindings":[]},{"name":"A","type":"Button","bindings":[]}]}]})", "A" },
				{ "duplicate map", R"({"version":1,"maps":[{"name":"M","actions":[]},{"name":"M","actions":[]}]})", "M" },
				{ "empty name", R"({"version":1,"maps":[{"name":"M","actions":[{"name":"","type":"Button","bindings":[]}]}]})", "" },
				{ "unknown type", R"({"version":1,"maps":[{"name":"M","actions":[{"name":"A","type":"Button3D","bindings":[]}]}]})", "Button3D" },
				{ "deadzone 1", R"({"version":1,"maps":[{"name":"M","actions":[{"name":"A","type":"Button","bindings":[{"control":"Key.A","deadzone":1.0}]}]}]})", "deadzone" },
			};
			for (const Reject& reject : rejects)
			{
				InputMapData untouched = sample_map_data();
				std::string rejectError;
				const bool ok = input_map_from_text(reject.Text, untouched, rejectError);
				check(!ok && !rejectError.empty() && same_map_data(untouched, sample_map_data()) &&
					rejectError.find(reject.Offender) != std::string::npos,
					std::string("map_rejects: ") + reject.Name + " (error: " + rejectError + ")");
			}
		}

		struct ActionRig
		{
			ScriptedInputBackend Backend;
			InputState State;
			ActionSystem Actions;

			void frame()
			{
				State.update(Backend);
				Actions.evaluate(State);
			}
		};

		Binding pad_control(PadAxis axis, float scale = 1.0f, float deadzone = 0.0f, bool invert = false)
		{
			Binding binding;
			binding.BindingKind = Binding::Kind::Control;
			binding.Parts[0] = control(axis);
			binding.Scale = scale;
			binding.Deadzone = deadzone;
			binding.Invert = invert;
			return binding;
		}

		std::vector<ActionMap> action_test_maps()
		{
			ActionMap gameplay;
			gameplay.Name = "Gameplay";
			gameplay.Actions.push_back({ "Jump", ActionType::Button, { control_binding(Key::Space) } });
			gameplay.Actions.push_back({ "Throttle", ActionType::Axis1D, { composite1d(Key::A, Key::D) } });
			gameplay.Actions.push_back({ "Move", ActionType::Axis2D, { composite2d(Key::W, Key::S, Key::A, Key::D) } });
			gameplay.Actions.push_back({ "Look", ActionType::Axis1D, { pad_control(PadAxis::LeftX, 1.0f, 0.2f) } });
			gameplay.Actions.push_back({ "Boost", ActionType::Axis1D, { pad_control(PadAxis::RightX, 2.0f, 0.0f, true) } });
			gameplay.Actions.push_back({ "Slow", ActionType::Axis1D, { control_binding(Key::Up, 0.5f) } });
			gameplay.Actions.push_back({ "Forward", ActionType::Axis1D, { control_binding(Key::W), pad_control(PadAxis::LeftY) } });
			gameplay.Actions.push_back({ "Fire", ActionType::Button, { pad_control(PadAxis::RightTrigger) } });
			gameplay.Actions.push_back({ "Aim", ActionType::Axis2D, { pair2d(PadAxis::LeftX, PadAxis::LeftY, 0.2f) } });
			gameplay.Actions.push_back({ "Interact", ActionType::Button, { control_binding(Key::E) } });

			ActionMap ui;
			ui.Name = "UI";
			ui.Consume = true;
			ui.Actions.push_back({ "Submit", ActionType::Button, { control_binding(Key::Space) } });

			ActionMap menu;
			menu.Name = "Menu";
			return { gameplay, ui, menu };
		}

		void action_evaluation(Check& check)
		{
			{
				ActionRig rig;
				rig.Actions.set_maps(action_test_maps());
				rig.Actions.push_map("Gameplay");

				rig.Backend.key(Key::Space, true);
				rig.frame();
				ActionState jump = rig.Actions.state("Gameplay", "Jump");
				check(jump.Down && jump.Pressed && !jump.Released, "act_button: first frame is down + pressed");
				rig.frame();
				jump = rig.Actions.state("Gameplay", "Jump");
				check(jump.Down && !jump.Pressed, "act_button: held is down only");
				rig.Backend.key(Key::Space, false);
				rig.frame();
				jump = rig.Actions.state("Gameplay", "Jump");
				check(!jump.Down && jump.Released && !jump.Pressed, "act_button: release");
				rig.frame();
				check(!rig.Actions.state("Gameplay", "Jump").Released, "act_button: the release lasts one frame");
			}

			{
				ActionRig rig;
				rig.Actions.set_maps(action_test_maps());
				rig.Actions.push_map("Gameplay");

				rig.Backend.key(Key::Space, true);
				rig.Backend.key(Key::Space, false);
				rig.frame();
				const ActionState jump = rig.Actions.state("Gameplay", "Jump");
				check(jump.Pressed && jump.Released && !jump.Down, "act_tap: a press and release inside one frame gives both edges");
			}

			{
				ActionRig rig;
				rig.Actions.set_maps(action_test_maps());
				rig.Actions.push_map("Gameplay");

				rig.Backend.key(Key::D, true);
				rig.frame();
				check(near_equal(rig.Actions.state("Gameplay", "Throttle").Value, 1.0f), "act_axis1d: positive key is +1");
				rig.Backend.key(Key::D, false);
				rig.Backend.key(Key::A, true);
				rig.frame();
				check(near_equal(rig.Actions.state("Gameplay", "Throttle").Value, -1.0f), "act_axis1d: negative key is -1");
				rig.Backend.key(Key::D, true);
				rig.frame();
				check(near_equal(rig.Actions.state("Gameplay", "Throttle").Value, 0.0f), "act_axis1d: both keys cancel");
			}

			{
				ActionRig rig;
				rig.Actions.set_maps(action_test_maps());
				rig.Actions.push_map("Gameplay");

				rig.Backend.key(Key::W, true);
				rig.Backend.key(Key::D, true);
				rig.frame();
				const ActionState move = rig.Actions.state("Gameplay", "Move");
				check(near_equal(move.Vector.x, 0.70710678f) && near_equal(move.Vector.y, 0.70710678f) && near_equal(glm::length(move.Vector), 1.0f),
					"act_axis2d_wasd: W+D is a unit diagonal");
				rig.Backend.key(Key::D, false);
				rig.frame();
				const ActionState forward = rig.Actions.state("Gameplay", "Move");
				check(near_equal(forward.Vector.x, 0.0f) && near_equal(forward.Vector.y, 1.0f), "act_axis2d_wasd: W alone is straight up");
			}

			{
				ActionRig rig;
				rig.Actions.set_maps(action_test_maps());
				rig.Actions.push_map("Gameplay");

				rig.Backend.connect_pad(0, true);
				rig.Backend.pad_axis(0, PadAxis::LeftX, 0.1f);
				rig.frame();
				check(near_equal(rig.Actions.state("Gameplay", "Look").Value, 0.0f), "act_stick_deadzone: inside the deadzone is zero");
				rig.Backend.pad_axis(0, PadAxis::LeftX, 0.6f);
				rig.frame();
				check(near_equal(rig.Actions.state("Gameplay", "Look").Value, (0.6f - 0.2f) / 0.8f), "act_stick_deadzone: outside is rescaled");
				rig.Backend.pad_axis(0, PadAxis::LeftX, -0.6f);
				rig.frame();
				check(near_equal(rig.Actions.state("Gameplay", "Look").Value, -(0.6f - 0.2f) / 0.8f), "act_stick_deadzone: keeps the sign");

				rig.Backend.pad_axis(0, PadAxis::LeftX, 0.1f);
				rig.Backend.pad_axis(0, PadAxis::LeftY, 0.1f);
				rig.frame();
				check(glm::length(rig.Actions.state("Gameplay", "Aim").Vector) < 1e-5f, "act_stick_deadzone: the pair has a radial deadzone");
				rig.Backend.pad_axis(0, PadAxis::LeftX, 0.6f);
				rig.Backend.pad_axis(0, PadAxis::LeftY, 0.0f);
				rig.frame();
				check(near_equal(rig.Actions.state("Gameplay", "Aim").Vector.x, 0.5f), "act_stick_deadzone: radial rescale");

				rig.Backend.pad_axis(0, PadAxis::RightX, 0.5f);
				rig.Backend.key(Key::Up, true);
				rig.frame();
				check(near_equal(rig.Actions.state("Gameplay", "Boost").Value, -1.0f), "act_scale_invert: scale 2 and invert");
				check(near_equal(rig.Actions.state("Gameplay", "Slow").Value, 0.5f), "act_scale_invert: a key with scale 0.5");
			}

			{
				ActionRig rig;
				rig.Actions.set_maps(action_test_maps());
				rig.Actions.push_map("Gameplay");

				rig.Backend.connect_pad(0, true);
				rig.Backend.key(Key::W, true);
				rig.Backend.pad_axis(0, PadAxis::LeftY, 0.3f);
				rig.frame();
				check(near_equal(rig.Actions.state("Gameplay", "Forward").Value, 1.0f), "act_largest_wins: keyboard 1.0 beats stick 0.3");
				rig.Backend.key(Key::W, false);
				rig.Backend.pad_axis(0, PadAxis::LeftY, 0.9f);
				rig.frame();
				check(near_equal(rig.Actions.state("Gameplay", "Forward").Value, 0.9f), "act_largest_wins: stick 0.9 beats an idle key");
			}

			{
				ActionRig rig;
				rig.Actions.set_maps(action_test_maps());
				rig.Actions.push_map("Gameplay");

				rig.Backend.connect_pad(0, true);
				rig.Backend.pad_axis(0, PadAxis::RightTrigger, 0.6f);
				rig.frame();
				check(rig.Actions.state("Gameplay", "Fire").Down && rig.Actions.state("Gameplay", "Fire").Pressed, "act_trigger_button: 0.6 presses");
				rig.Backend.pad_axis(0, PadAxis::RightTrigger, 0.4f);
				rig.frame();
				check(!rig.Actions.state("Gameplay", "Fire").Down && rig.Actions.state("Gameplay", "Fire").Released, "act_trigger_button: 0.4 releases");
			}

			{
				ActionRig rig;
				rig.Actions.set_maps(action_test_maps());
				rig.Actions.push_map("Gameplay");
				rig.Backend.key(Key::Space, true);
				rig.frame();

				const ActionState unknown = rig.Actions.state("Gameplay", "Nope");
				const ActionState unknownMap = rig.Actions.state("Nope", "Jump");
				check(!unknown.Down && near_equal(unknown.Value, 0.0f) && !unknownMap.Down && near_equal(unknownMap.Value, 0.0f), "act_unknown: unknown names give a zero state");
				check(!rig.Actions.has_action("Gameplay", "Nope") && !rig.Actions.has_action("Nope", "Jump") && rig.Actions.has_action("Gameplay", "Jump"), "act_unknown: has_action");
				check(!rig.Actions.state("UI", "Submit").Down, "act_unknown: an inactive map reads zero");
			}

			{
				ActionRig rig;
				rig.Actions.set_maps(action_test_maps());
				rig.Actions.push_map("Gameplay");
				rig.Actions.push_map("UI");
				rig.Actions.push_map("Menu");
				check(rig.Actions.map_active("Gameplay") && rig.Actions.map_active("UI") && rig.Actions.map_active("Menu") && !rig.Actions.map_active("Nope"), "act_stack: pushed maps are active");
				check(rig.Actions.active_maps() == std::vector<std::string>({ "Menu", "UI", "Gameplay" }), "act_stack: top first");
				rig.Actions.pop_map("UI");
				check(rig.Actions.active_maps() == std::vector<std::string>({ "Menu", "Gameplay" }) && !rig.Actions.map_active("UI"), "act_stack: pop works from the middle");
				rig.Actions.pop_map("Nope");
				check(rig.Actions.active_maps().size() == 2, "act_stack: popping an inactive map does nothing");
			}

			{
				ActionRig rig;
				rig.Actions.set_maps(action_test_maps());
				rig.Actions.push_map("Gameplay");
				rig.Backend.key(Key::Space, true);
				rig.frame();
				check(rig.Actions.state("Gameplay", "Jump").Down, "act_consume: Jump reads Space before the UI map");

				rig.Actions.push_map("UI");
				rig.frame();
				check(!rig.Actions.state("Gameplay", "Jump").Down && near_equal(rig.Actions.state("Gameplay", "Jump").Value, 0.0f) && rig.Actions.state("UI", "Submit").Down,
					"act_consume: the consuming UI map takes Space from Gameplay");
				rig.Actions.pop_map("UI");
				rig.frame();
				check(rig.Actions.state("Gameplay", "Jump").Down, "act_consume: Gameplay reads Space again after the pop");
			}
		}

		std::string maps_text(const ActionSystem& actions)
		{
			return input_map_to_text(InputMapData{ actions.maps() });
		}

		void action_rebinding(Check& check)
		{
			const InputControl keyE = control(Key::E);
			const InputControl space = control(Key::Space);

			{
				ActionRig rig;
				rig.Actions.set_maps(action_test_maps());
				rig.Actions.push_map("Gameplay");
				check(!rig.Actions.modified("Gameplay", "Jump"), "rebind_basic: not modified at the start");
				check(rig.Actions.rebind("Gameplay", "Jump", 0, keyE), "rebind_basic: rebind succeeds");
				check(rig.Actions.modified("Gameplay", "Jump"), "rebind_basic: now modified");

				rig.Backend.key(Key::E, true);
				rig.frame();
				check(rig.Actions.state("Gameplay", "Jump").Down, "rebind_basic: E triggers Jump");
				rig.Backend.key(Key::E, false);
				rig.Backend.key(Key::Space, true);
				rig.frame();
				check(!rig.Actions.state("Gameplay", "Jump").Down, "rebind_basic: Space no longer triggers Jump");
				const Action* jump = rig.Actions.action("Gameplay", "Jump");
				check(jump && jump->Bindings.size() == 1 && jump->Bindings[0].Parts[0] == keyE, "rebind_basic: action() shows the current binding");
			}

			{
				ActionRig rig;
				rig.Actions.set_maps(action_test_maps());
				rig.Actions.push_map("Gameplay");
				check(rig.Actions.rebind("Gameplay", "Move", 0, control(Key::I), "up"), "rebind_part: rebind the up part");
				rig.Backend.key(Key::I, true);
				rig.frame();
				check(near_equal(rig.Actions.state("Gameplay", "Move").Vector.y, 1.0f), "rebind_part: I moves up");
				rig.Backend.key(Key::I, false);
				rig.Backend.key(Key::W, true);
				rig.frame();
				check(near_equal(rig.Actions.state("Gameplay", "Move").Vector.y, 0.0f), "rebind_part: W no longer moves up");
				rig.Backend.key(Key::W, false);
				rig.Backend.key(Key::S, true);
				rig.frame();
				check(near_equal(rig.Actions.state("Gameplay", "Move").Vector.y, -1.0f), "rebind_part: the other parts are untouched");
			}

			{
				ActionRig rig;
				rig.Actions.set_maps(action_test_maps());
				const std::string before = maps_text(rig.Actions);
				check(!rig.Actions.rebind("Nope", "Jump", 0, keyE), "rebind_invalid: unknown map");
				check(!rig.Actions.rebind("Gameplay", "Nope", 0, keyE), "rebind_invalid: unknown action");
				check(!rig.Actions.rebind("Gameplay", "Jump", 5, keyE), "rebind_invalid: binding index out of range");
				check(!rig.Actions.rebind("Gameplay", "Jump", 0, keyE, "up"), "rebind_invalid: a part the binding does not have");
				check(!rig.Actions.rebind("Gameplay", "Move", 0, keyE, "sideways"), "rebind_invalid: unknown part name");
				check(maps_text(rig.Actions) == before, "rebind_invalid: nothing changed");
			}

			{
				ActionRig rig;
				rig.Actions.set_maps(action_test_maps());
				rig.Actions.push_map("Gameplay");

				const size_t added = rig.Actions.add_binding("Gameplay", "Jump", control_binding(Key::E));
				check(added == 1, "add_remove_binding: the new binding has index 1");
				check(rig.Actions.add_binding("Gameplay", "Jump", composite1d(Key::A, Key::D)) == SIZE_MAX, "add_remove_binding: a binding the type cannot use is refused");
				check(rig.Actions.add_binding("Gameplay", "Nope", control_binding(Key::E)) == SIZE_MAX, "add_remove_binding: unknown action");
				rig.Backend.key(Key::E, true);
				rig.frame();
				check(rig.Actions.state("Gameplay", "Jump").Down, "add_remove_binding: the added binding works");
				rig.Backend.key(Key::E, false);

				check(rig.Actions.remove_binding("Gameplay", "Jump", 0), "add_remove_binding: remove the first binding");
				check(!rig.Actions.remove_binding("Gameplay", "Jump", 3), "add_remove_binding: remove out of range fails");
				rig.Backend.key(Key::Space, true);
				rig.frame();
				check(!rig.Actions.state("Gameplay", "Jump").Down, "add_remove_binding: Space no longer works");
				check(rig.Actions.modified("Gameplay", "Jump"), "add_remove_binding: modified");
			}

			{
				ActionRig rig;
				rig.Actions.set_maps(action_test_maps());
				const std::vector<BindingConflict> conflicts = rig.Actions.conflicts("Gameplay", keyE, "Jump");
				check(conflicts.size() == 1 && conflicts[0].Action == "Interact" && conflicts[0].Binding == 0 && conflicts[0].Map == "Gameplay", "conflicts_reported: Interact uses E");
				check(rig.Actions.conflicts("Gameplay", space, "Jump").empty(), "conflicts_reported: the excepted action is not a conflict");
				check(rig.Actions.conflicts("Gameplay", space).size() == 1, "conflicts_reported: another map does not count");
				check(rig.Actions.conflicts("Gameplay", control(Key::D)).size() >= 2, "conflicts_reported: parts of composites count");
				check(rig.Actions.rebind("Gameplay", "Jump", 0, keyE), "conflicts_reported: the rebind still applies");
				check(rig.Actions.conflicts("Gameplay", keyE).size() == 2, "conflicts_reported: now both use E");
			}

			{
				ActionRig rig;
				rig.Actions.set_maps(action_test_maps());
				const std::string defaults = maps_text(rig.Actions);
				rig.Actions.rebind("Gameplay", "Jump", 0, keyE);
				rig.Actions.rebind("Gameplay", "Move", 0, control(Key::I), "up");
				check(rig.Actions.reset_action("Gameplay", "Jump") && !rig.Actions.modified("Gameplay", "Jump") && rig.Actions.modified("Gameplay", "Move"),
					"reset_action: restores one action only");
				check(rig.Actions.action("Gameplay", "Jump")->Bindings[0].Parts[0] == space, "reset_action: the default binding is back");
				check(!rig.Actions.reset_action("Gameplay", "Nope"), "reset_action: unknown action");
				rig.Actions.reset_all();
				check(maps_text(rig.Actions) == defaults && !rig.Actions.modified("Gameplay", "Move"), "reset_all: everything is back to the defaults");
			}
		}

		void action_listen(Check& check)
		{
			{
				ActionRig rig;
				rig.Actions.set_maps(action_test_maps());
				std::vector<InputControl> got;
				rig.Actions.listen_for_control([&](const InputControl& control) { got.push_back(control); });
				check(rig.Actions.listening(), "listen_key: listening after the call");
				rig.frame();
				check(got.empty() && rig.Actions.listening(), "listen_key: nothing happens without input");
				rig.Backend.key(Key::K, true);
				rig.frame();
				check(got.size() == 1 && got[0] == control(Key::K) && !rig.Actions.listening(), "listen_key: the pressed key is captured and listening ends");
			}

			{
				ActionRig rig;
				rig.Actions.set_maps(action_test_maps());
				std::vector<InputControl> got;
				rig.Backend.connect_pad(0, true);
				rig.Actions.listen_for_control([&](const InputControl& control) { got.push_back(control); });
				rig.Backend.pad_axis(0, PadAxis::LeftX, 0.4f);
				rig.frame();
				check(got.empty() && rig.Actions.listening(), "listen_pad_axis: 0.4 is ignored");
				rig.Backend.pad_axis(0, PadAxis::LeftX, 0.6f);
				rig.frame();
				check(got.size() == 1 && got[0] == control(PadAxis::LeftX), "listen_pad_axis: 0.6 is captured");
			}

			{
				ActionRig rig;
				rig.Actions.set_maps(action_test_maps());
				int calls = 0;
				rig.Actions.listen_for_control([&](const InputControl&) { calls++; });
				rig.Backend.key(Key::Escape, true);
				rig.frame();
				check(calls == 0 && !rig.Actions.listening(), "listen_escape_cancels: Escape cancels without a callback");
			}

			{
				ActionRig rig;
				rig.Actions.set_maps(action_test_maps());
				int calls = 0;
				rig.Actions.listen_for_control([&](const InputControl&) { calls++; });
				rig.Backend.key(Key::K, true);
				rig.Backend.key(Key::L, true);
				rig.frame();
				rig.Backend.key(Key::M, true);
				rig.frame();
				rig.frame();
				check(calls == 1, "listen_once: the callback runs exactly once");
			}

			{
				ActionRig rig;
				rig.Actions.set_maps(action_test_maps());
				rig.Actions.push_map("Gameplay");
				rig.Backend.connect_pad(0, true);
				rig.Backend.pad_axis(0, PadAxis::LeftY, 0.4f);
				rig.frame();
				check(near_equal(rig.Actions.state("Gameplay", "Forward").Value, 0.4f), "listen_mutes_actions: the stick drives Forward before listening");
				rig.Actions.listen_for_control([](const InputControl&) {});
				rig.frame();
				check(near_equal(rig.Actions.state("Gameplay", "Forward").Value, 0.0f), "listen_mutes_actions: actions are zero while listening");
				rig.Actions.cancel_listen();
				check(!rig.Actions.listening(), "listen_mutes_actions: cancel_listen");
				rig.frame();
				check(near_equal(rig.Actions.state("Gameplay", "Forward").Value, 0.4f), "listen_mutes_actions: actions come back");
			}
		}

		void last_device(Check& check)
		{
			ScriptedInputBackend backend;
			InputState state;

			state.update(backend);
			check(state.last_device() == LastDevice::KeyboardMouse, "last_device_switch: the default is keyboard and mouse");
			backend.connect_pad(0, true);
			backend.pad_axis(0, PadAxis::LeftX, 0.5f);
			state.update(backend);
			check(state.last_device() == LastDevice::Gamepad, "last_device_switch: a stick at 0.5 switches to gamepad");
			backend.pad_axis(0, PadAxis::LeftX, 0.1f);
			state.update(backend);
			check(state.last_device() == LastDevice::Gamepad, "last_device_switch: noise changes nothing");
			backend.key(Key::W, true);
			state.update(backend);
			check(state.last_device() == LastDevice::KeyboardMouse, "last_device_switch: a key switches back");
			state.update(backend);
			backend.pad_axis(0, PadAxis::LeftX, 0.2f);
			state.update(backend);
			check(state.last_device() == LastDevice::KeyboardMouse, "last_device_switch: stick noise below 0.3 keeps keyboard");
			backend.pad_button(0, PadButton::A, true);
			state.update(backend);
			check(state.last_device() == LastDevice::Gamepad, "last_device_switch: a pad button switches to gamepad");
			backend.move_cursor({ 30.0f, 30.0f });
			state.update(backend);
			check(state.last_device() == LastDevice::KeyboardMouse, "last_device_switch: the mouse moving switches back");
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
		input_map_format(check);
		action_evaluation(check);
		action_rebinding(check);
		action_listen(check);
		last_device(check);

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
