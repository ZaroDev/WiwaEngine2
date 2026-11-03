#include <editor_common.h>
#include "input_smoke_test.h"

#include "../asset_pipeline/asset_database.h"
#include "input_edit_model.h"
#include "../asset_pipeline/asset_templates.h"
#include "../asset_pipeline/file_utils.h"
#include "../asset_pipeline/input_settings_file.h"
#include "../asset_pipeline/meta_file.h"
#include <assets/pak_source.h>
#include <assets/wasset_io.h>
#include <core/engine.h>
#include <input/action_system.h>
#include <input/action_types.h>
#include <input/input_codes.h>
#include <input/input_ui_source.h>
#include <input/input_map_io.h>
#include <input/input_overrides.h>
#include <input/input_project_settings.h>
#include <input/input_module.h>
#include <input/input_state.h>
#include <scripting/script_instances.h>
#include <scripting/script_module.h>
#include <scripting/script_runtime.h>
#include <scripting/script_source.h>
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
				ScriptedInputBackend backend;
				InputState state;
				backend.connect_pad(0, true);
				backend.connect_pad(1, true);
				backend.pad_axis(0, PadAxis::LeftX, 0.5f);
				backend.pad_axis(1, PadAxis::LeftX, -0.9f);
				backend.pad_button(1, PadButton::A, true);
				state.update(backend);
				check(near_equal(state.value(padLeftX, 0), 0.5f) && near_equal(state.value(padLeftX, 1), -0.9f) && near_equal(state.value(padLeftX, 2), 0.0f) && near_equal(state.value(padLeftX), -0.9f),
					"raw_pad_select: a pad argument reads that pad only");
				check(!state.down(padA, 0) && state.down(padA, 1) && state.pressed(padA, 1) && !state.pressed(padA, 0) && state.down(padA), "raw_pad_select: buttons too");
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

		void write_text_file(const std::filesystem::path& path, const std::string& text)
		{
			std::ofstream file(path, std::ios::binary | std::ios::trunc);
			file << text;
		}

		void input_persistence(Check& check, AssetPipelineModule& pipeline)
		{
			std::error_code error;
			const std::filesystem::path dir = std::filesystem::temp_directory_path() / "wiwa_input_smoke";
			std::filesystem::remove_all(dir, error);
			std::filesystem::create_directories(dir, error);

			// The cooked asset: a version and the JSON text.
			{
				const InputMapData sample = sample_map_data();
				const std::vector<std::byte> bytes = serialize(sample);
				const std::optional<InputMapData> back = deserialize<InputMapData>(bytes);
				check(back && same_map_data(*back, sample), "asset_archive_roundtrip: serialize -> deserialize is equal");

				std::vector<std::byte> wrongVersion = bytes;
				wrongVersion[0] = std::byte{ 2 };
				check(!deserialize<InputMapData>(wrongVersion), "asset_archive_roundtrip: another version is refused");

				const std::vector<std::byte> truncated(bytes.begin(), bytes.end() - 3);
				check(!deserialize<InputMapData>(truncated), "asset_archive_roundtrip: a truncated payload is refused");

				std::vector<std::byte> trailing = bytes;
				trailing.push_back(std::byte{ 0 });
				check(!deserialize<InputMapData>(trailing), "asset_archive_roundtrip: trailing bytes are refused");

				BinaryArchive writer = BinaryArchive::writer();
				u32 version = INPUT_MAP_VERSION;
				std::string garbage = "{{{";
				writer.value(version);
				writer.string(garbage);
				const std::vector<std::byte> badText = writer.take();
				check(!deserialize<InputMapData>(badText), "asset_archive_roundtrip: text that is not a valid map is refused");
			}

			// The override file: only the changed actions, and they come back.
			{
				ActionSystem actions;
				actions.set_maps(action_test_maps());
				actions.rebind("Gameplay", "Jump", 0, control(Key::E));
				actions.rebind("Gameplay", "Move", 0, control(Key::I), "up");
				actions.add_binding("Gameplay", "Slow", control_binding(Key::J));

				const nlohmann::json json = overrides_to_json(actions);
				const nlohmann::json& gameplay = json["maps"]["Gameplay"];
				check(gameplay.contains("Jump") && gameplay.contains("Move") && gameplay.contains("Slow") && !gameplay.contains("Throttle") && !json["maps"].contains("UI"),
					"overrides_roundtrip: only the changed actions are in the JSON");

				ActionSystem fresh;
				fresh.set_maps(action_test_maps());
				std::string warnings;
				const size_t applied = apply_overrides(fresh, json, warnings);
				check(applied == 3 && warnings.empty(), "overrides_roundtrip: all three are applied without warnings (" + warnings + ")");
				check(maps_text(fresh) == maps_text(actions) && fresh.modified("Gameplay", "Jump") && !fresh.modified("Gameplay", "Throttle"),
					"overrides_roundtrip: the bindings are the same");
			}

			// A stale override file never breaks the defaults.
			{
				ActionSystem actions;
				actions.set_maps(action_test_maps());
				const std::string defaults = maps_text(actions);

				const char* stale = R"({"version":1,"maps":{
					"Nope":{"Jump":[{"control":"Key.E"}]},
					"Gameplay":{"Nope":[{"control":"Key.E"}],
						"Jump":[{"negative":"Key.A","positive":"Key.D"}],
						"Throttle":[{"control":"Key.Nope"}],
						"Interact":[{"control":"Key.R"}]}}})";
				bool threw = false;
				size_t applied = 99;
				std::string warnings;
				try
				{
					applied = apply_overrides(actions, nlohmann::json::parse(stale), warnings);
				}
				catch (...)
				{
					threw = true;
				}
				check(!threw && applied == 1 && !warnings.empty(), "overrides_stale: stale entries are skipped with warnings, the valid one applies");
				check(actions.action("Gameplay", "Interact")->Bindings[0].Parts[0] == control(Key::R) && !actions.modified("Gameplay", "Jump") && !actions.modified("Gameplay", "Throttle"),
					"overrides_stale: the stale entries left the defaults alone");

				for (const char* bad : { R"({"version":2,"maps":{"Gameplay":{"Interact":[{"control":"Key.R"}]}}})", "[1,2,3]", "\"text\"", R"({"version":1,"maps":[]})" })
				{
					ActionSystem untouched;
					untouched.set_maps(action_test_maps());
					std::string badWarnings;
					size_t count = 99;
					try
					{
						count = apply_overrides(untouched, nlohmann::json::parse(bad), badWarnings);
					}
					catch (...)
					{
						threw = true;
					}
					check(!threw && count == 0 && !badWarnings.empty() && maps_text(untouched) == defaults, std::string("overrides_stale: refused whole: ") + bad);
				}
			}

			// The module saves and loads the file, and a file that is garbage leaves the defaults.
			{
				InputModule module;
				module.add_maps(action_test_maps());
				module.set_overrides_path(dir / "saved.json");
				module.actions().rebind("Gameplay", "Jump", 0, control(Key::E));
				check(module.save_overrides(), "overrides_file: save");

				InputModule other;
				other.add_maps(action_test_maps());
				other.set_overrides_path(dir / "saved.json");
				check(other.load_overrides() && other.actions().modified("Gameplay", "Jump") && other.actions().action("Gameplay", "Jump")->Bindings[0].Parts[0] == control(Key::E),
					"overrides_file: a fresh module loads the saved bindings");

				const std::string defaults = maps_text(other.actions());
				for (const char* content : { "{{{", "" })
				{
					InputModule garbage;
					garbage.add_maps(action_test_maps());
					write_text_file(dir / "garbage.json", content);
					garbage.set_overrides_path(dir / "garbage.json");
					check(!garbage.load_overrides() && garbage.actions().find_map("Gameplay") && !garbage.actions().modified("Gameplay", "Jump"),
						std::string("overrides_file_garbage: '") + content + "' leaves the defaults");
				}
				InputModule missing;
				missing.add_maps(action_test_maps());
				missing.set_overrides_path(dir / "missing.json");
				check(!missing.load_overrides() && !missing.actions().modified("Gameplay", "Jump"), "overrides_file_garbage: a missing file leaves the defaults");
			}

			// The project settings: the file, and the pack.
			{
				const std::filesystem::path file = dir / "input.wsettings";
				InputProjectSettings settings;
				settings.DefaultMap.Guid = AssetGuid::generate();

				std::string settingsError;
				check(save_input_settings(file, settings, settingsError), "settings_roundtrip: save");
				InputProjectSettings loaded;
				check(load_input_settings(file, loaded, settingsError) && loaded.DefaultMap == settings.DefaultMap, "settings_roundtrip: the default map comes back");

				InputProjectSettings none;
				none.DefaultMap.Guid = AssetGuid::generate();
				check(load_input_settings(dir / "none.wsettings", none, settingsError) && !none.DefaultMap.valid(), "settings_roundtrip: a missing file is the default");
				write_text_file(dir / "garbage.wsettings", "{{{");
				check(!load_input_settings(dir / "garbage.wsettings", none, settingsError) && !settingsError.empty(), "settings_roundtrip: a damaged file is refused");

				AssetDatabase& database = pipeline.get_database();
				const std::filesystem::path noPhysics = dir / "no_physics.wsettings";
				check(Packager::pack(database, dir / "pack", nullptr, nullptr, noPhysics, file), "settings_pack: packing with the file");
				PakSource pak(dir / "pack" / "assets.wpak");
				const std::optional<InputProjectSettings> fromPack = pak.valid() ? read_input_settings(pak) : std::nullopt;
				check(fromPack && fromPack->DefaultMap == settings.DefaultMap, "settings_pack: the pack holds the default map");

				check(Packager::pack(database, dir / "pack_without", nullptr, nullptr, noPhysics, dir / "none.wsettings"), "settings_pack: packing without the file");
				PakSource plain(dir / "pack_without" / "assets.wpak");
				check(plain.valid() && !read_input_settings(plain).has_value(), "settings_pack: no file, no settings in the pack");
			}

			std::filesystem::remove_all(dir, error);
		}

		// The cooked asset of a source file, read from the Library (nullopt if there is none).
		std::optional<std::filesystem::path> cooked_path(AssetDatabase& database, const std::filesystem::path& source)
		{
			std::optional<MetaFile> meta = MetaFile::load(MetaFile::meta_path_for(source));
			if (!meta) return std::nullopt;

			const std::string name = meta->Guid.to_string() + ".wasset";
			std::error_code error;
			for (auto it = std::filesystem::recursive_directory_iterator(database.get_library_root(), error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
			{
				if (it->is_regular_file() && it->path().filename() == name) return it->path();
			}
			return std::nullopt;
		}

		std::optional<InputMapData> cooked_input_map(AssetDatabase& database, const std::filesystem::path& source, std::vector<std::byte>* bytesOut = nullptr)
		{
			const std::optional<std::filesystem::path> path = cooked_path(database, source);
			if (!path) return std::nullopt;

			const std::optional<std::vector<std::byte>> bytes = read_binary_file(*path);
			std::span<const std::byte> payload;
			std::optional<WAssetHeader> header = bytes ? parse_wasset_header(*bytes, payload) : std::nullopt;
			if (!header || header->Type != AssetType::InputMap) return std::nullopt;

			if (bytesOut) *bytesOut = *bytes;
			return deserialize<InputMapData>(payload);
		}

		void input_importer(Check& check, AssetPipelineModule& pipeline)
		{
			AssetDatabase& database = pipeline.get_database();
			std::error_code error;
			const std::filesystem::path dir = database.get_assets_root() / "zz_input_smoke";
			std::filesystem::remove_all(dir, error);
			std::filesystem::create_directories(dir, error);

			const std::filesystem::path file = dir / "test.winput";
			const InputMapData sample = sample_map_data();
			write_text_file(file, input_map_to_text(sample));

			std::vector<std::byte> libraryBytes;
			const std::optional<AssetRef> ref = database.ensure_imported(file);
			check(ref.has_value(), "import_input_map: the .winput imports");
			const std::optional<InputMapData> cooked = cooked_input_map(database, file, &libraryBytes);
			check(cooked && same_map_data(*cooked, sample), "import_input_map: the cooked map equals the source");

			// the pack holds the Library's bytes and they decode to the same map
			if (ref)
			{
				const std::filesystem::path packDirectory = std::filesystem::temp_directory_path() / "wiwa_input_import_smoke";
				std::filesystem::remove_all(packDirectory, error);
				check(Packager::pack(database, packDirectory, nullptr, nullptr, packDirectory / "none_physics.wsettings", packDirectory / "none_input.wsettings"),
					"import_input_map_pack: packing the Library with a map in it");
				PakSource pak(packDirectory / "assets.wpak");
				const std::optional<std::vector<std::byte>> packed = pak.valid() ? pak.read(ref->Guid) : std::nullopt;
				check(packed && *packed == libraryBytes, "import_input_map_pack: the pack holds the bytes of the Library");
				std::span<const std::byte> payload;
				const std::optional<InputMapData> fromPack = packed && parse_wasset_header(*packed, payload) ? deserialize<InputMapData>(payload) : std::nullopt;
				check(fromPack && same_map_data(*fromPack, sample), "import_input_map_pack: the packed map decodes to the source");

				// a build that packs only what its scenes use still carries the project's default input map (no scene refers to it)
				{
					const std::filesystem::path settingsFile = packDirectory / "input.wsettings";
					std::filesystem::create_directories(packDirectory, error);
					InputProjectSettings projectSettings;
					projectSettings.DefaultMap = *ref;
					std::string settingsError;
					check(save_input_settings(settingsFile, projectSettings, settingsError), "import_input_map_default_packed: the settings are saved");

					const BuildSettings noScenes{};
					const std::filesystem::path selectivePack = packDirectory / "selective";
					check(Packager::pack(database, selectivePack, nullptr, &noScenes, packDirectory / "none_physics.wsettings", settingsFile), "import_input_map_default_packed: packing a build with no scenes");
					PakSource selective(selectivePack / "assets.wpak");
					check(selective.valid() && selective.contains(ref->Guid), "import_input_map_default_packed: the default input map is in the pack");
					const std::optional<InputProjectSettings> packedSettings = selective.valid() ? read_input_settings(selective) : std::nullopt;
					check(packedSettings && packedSettings->DefaultMap == *ref, "import_input_map_default_packed: and the pack says it is the default");
				}
				std::filesystem::remove_all(packDirectory, error);
			}

			// the guid survives an edit and a re-import
			InputMapData edited = sample;
			edited.Maps.push_back(ActionMap{ "Extra", false, {} });
			write_text_file(file, input_map_to_text(edited));
			database.reimport(file);
			const std::optional<AssetRef> again = database.ensure_imported(file);
			check(ref && again && again->Guid == ref->Guid, "import_input_map: a re-import keeps the guid");
			const std::optional<InputMapData> reimported = cooked_input_map(database, file);
			check(reimported && same_map_data(*reimported, edited), "import_input_map: the edit is cooked");

			// a damaged file fails the import and the previous cooked version stays
			WI_INPUT_LOG(Info, "Smoke test: the next import errors are expected");
			write_text_file(file, "{{{ not a map");
			database.reimport(file);
			const std::optional<InputMapData> kept = cooked_input_map(database, file);
			check(kept && same_map_data(*kept, edited), "import_input_map_bad: a damaged edit keeps the previous cooked map");

			const std::filesystem::path broken = dir / "broken.winput";
			write_text_file(broken, R"({"version":1,"maps":[{"name":"M","actions":[{"name":"A","type":"Button","bindings":[{"control":"Key.Nope"}]}]}]})");
			database.ensure_imported(broken);
			check(!cooked_input_map(database, broken).has_value(), "import_input_map_bad: an unknown control cooks nothing");

			// the Create menu makes a file that imports
			check(std::string(extension(CreateKind::InputMap)) == ".winput", "import_input_map_template: the Create menu kind has the extension");
			const std::filesystem::path created = create_asset(CreateKind::InputMap, dir, "Made");
			check(!created.empty() && database.ensure_imported(created).has_value(), "import_input_map_template: the created file imports");
			const std::optional<InputMapData> made = cooked_input_map(database, created);
			check(made && made->Maps.size() == 1 && made->Maps[0].Name == "Gameplay" && made->Maps[0].Actions.size() == 2 &&
				made->Maps[0].Actions[0].Name == "Jump" && made->Maps[0].Actions[1].Name == "Move", "import_input_map_template: the template has Gameplay with Jump and Move");

			std::filesystem::remove_all(dir, error);
			database.scan();
		}

		class NoScripts final : public IScriptSource
		{
		public:
			virtual std::optional<LoadedScript> find_by_path(std::string_view) override { return std::nullopt; }
			virtual std::optional<LoadedScript> find_by_asset(AssetRef) override { return std::nullopt; }
		};

		// A Lua runtime with the input API bound to a module that reads from a scripted backend.
		struct LuaInputHarness
		{
			NoScripts Source;
			ScriptRuntime Runtime{ Source };
			InputModule Module;
			ScriptedInputBackend Backend;
			ScriptInstances Instances{ Runtime }; // after the module: it is destroyed first and may still cancel a listen
			sol::environment Env{ Runtime.state(), sol::create, Runtime.sandbox() };

			LuaInputHarness()
			{
				Module.add_maps(action_test_maps());
				Module.set_backend(&Backend);
				Module.actions().push_map("Gameplay");
				Instances.context().Input = &Module;
			}

			void frame() { Module.update(0.016f); }

			// Runs a chunk and gives its results as text ("" for none); `error` receives the Lua error, if any.
			std::string run(const std::string& chunk, std::string* error = nullptr)
			{
				sol::protected_function_result result = Runtime.state().safe_script(chunk, Env, sol::script_pass_on_error);
				if (!result.valid())
				{
					sol::error failure = result;
					if (error) *error = failure.what();
					return {};
				}
				if (error) error->clear();
				return result.get_type() == sol::type::none ? std::string() : Runtime.state()["tostring"](result.get<sol::object>()).get<std::string>();
			}

			bool fails(const std::string& chunk, const std::string& mention)
			{
				std::string error;
				run(chunk, &error);
				return !error.empty() && error.find(mention) != std::string::npos;
			}
		};

		void lua_input(Check& check)
		{
			{
				LuaInputHarness h;
				h.Backend.key(Key::W, true);
				h.Backend.connect_pad(0, true);
				h.Backend.connect_pad(1, true);
				h.Backend.pad_axis(0, PadAxis::LeftX, 0.5f);
				h.Backend.pad_axis(1, PadAxis::LeftX, -0.9f);
				h.Backend.pad_button(1, PadButton::A, true);
				h.Backend.move_cursor({ 12.0f, 34.0f });
				h.frame();
				check(h.run("return input.down('Key.W')") == "true" && h.run("return input.pressed('Key.W')") == "true" && h.run("return input.released('Key.W')") == "false",
					"lua_raw: down / pressed / released");
				h.frame();
				check(h.run("return input.down('Key.W')") == "true" && h.run("return input.pressed('Key.W')") == "false", "lua_raw: held is not pressed again");
				check(h.run("return input.value('Pad.LeftX')") == "-0.89999997615814" || h.run("return input.value('Pad.LeftX') < -0.89 and input.value('Pad.LeftX') > -0.91") == "true",
					"lua_raw: the largest pad wins without a pad argument");
				check(h.run("return input.value('Pad.LeftX', 0) > 0.49 and input.value('Pad.LeftX', 0) < 0.51") == "true" &&
					h.run("return input.value('Pad.LeftX', 1) < -0.89") == "true", "lua_raw: the pad argument is 0-based");
				check(h.run("return input.down('Pad.A', 0)") == "false" && h.run("return input.down('Pad.A', 1)") == "true" && h.run("return input.down('Pad.A')") == "true",
					"lua_raw: a pad button on the chosen pad only");
				check(h.run("return input.pad_connected(0) and input.pad_connected(1) and not input.pad_connected(2)") == "true", "lua_raw: pad_connected");
				check(h.run("local m = input.mouse(); return m.x .. ',' .. m.y") == "12.0,34.0" || h.run("local m = input.mouse(); return m.x == 12 and m.y == 34") == "true", "lua_raw: mouse position");
				check(h.run("return input.last_device()") == "keyboard_mouse" || h.run("return input.last_device()") == "gamepad", "lua_raw: last_device is one of the two names");
			}

			{
				LuaInputHarness h;
				h.Backend.key(Key::W, true);
				h.frame();
				check(h.fails("return input.down('Key.Nope')", "Key.Nope") && h.fails("return input.pressed('W')", "'W'") && h.fails("return input.value(5)", "string"),
					"lua_bad_control: the error names the bad control");
				check(h.fails("input.set_cursor('sideways')", "sideways"), "lua_bad_control: a bad cursor mode is an error");
				h.run("input.set_cursor('locked')");
				check(h.Backend.cursor_mode() == CursorMode::Locked, "lua_cursor: locked reaches the backend");
			}

			{
				LuaInputHarness h;
				h.Backend.key(Key::Space, true);
				h.Backend.key(Key::W, true);
				h.frame();
				check(h.run("local a = input.action('Gameplay', 'Jump'); return a:down() and a:pressed() and not a:released() and a:value() > 0.99") == "true", "lua_action: a handle follows a press");
				check(h.run("local v = input.action('Gameplay', 'Move'):vector(); return v.x == 0 and v.y > 0.99") == "true", "lua_action: vector()");
				h.frame();
				check(h.run("return input.action('Gameplay', 'Jump'):pressed()") == "false", "lua_action: the handle reads the current frame");
				h.Backend.key(Key::Space, false);
				h.frame();
				check(h.run("return input.action('Gameplay', 'Jump'):released()") == "true", "lua_action: released");
			}

			{
				LuaInputHarness h;
				check(h.fails("return input.action('Gameplay', 'Jum')", "Gameplay/Jum") && h.fails("return input.action('Nope', 'Jump')", "Nope/Jump"), "lua_action_unknown: the error names map/action");
			}

			{
				LuaInputHarness h;
				h.run("input.push_map('UI')");
				check(h.Module.actions().map_active("UI") && h.Module.actions().active_maps().front() == "UI", "lua_stack: push");
				h.run("input.pop_map('UI')");
				check(!h.Module.actions().map_active("UI") && h.Module.actions().map_active("Gameplay"), "lua_stack: pop");
				check(h.fails("input.push_map('Nope')", "Nope"), "lua_stack: an unknown map is an error");
			}

			{
				LuaInputHarness h;
				check(h.run("local b = input.bindings('Gameplay', 'Move'); return #b .. ':' .. b[1].kind .. ':' .. b[1].controls[1]") == "1:composite2d:Key.W", "lua_bindings: kind and controls");
				check(h.run("return input.bindings('Gameplay', 'Jump')[1].controls[1]") == "Key.Space", "lua_bindings: a plain control");

				const std::string conflict = h.run("local ok, conflicts = input.rebind('Gameplay', 'Jump', 1, 'Key.E'); return tostring(ok) .. ':' .. #conflicts .. ':' .. conflicts[1].action .. ':' .. conflicts[1].map");
				check(conflict == "false:1:Interact:Gameplay", "lua_rebind: a conflict is returned (" + conflict + ")");
				h.Backend.key(Key::E, true);
				h.frame();
				check(h.Module.actions().state("Gameplay", "Jump").Down, "lua_rebind: the rebind was applied anyway");
				check(h.run("return input.rebind('Gameplay', 'Jump', 1, 'Key.J')") == "true", "lua_rebind: no conflict gives true");
				check(h.run("return input.rebind('Gameplay', 'Move', 1, 'Key.I', 'up')") == "true" && h.Module.actions().action("Gameplay", "Move")->Bindings[0].Parts[0] == control(Key::I),
					"lua_rebind: a part of a composite");
				check(h.fails("input.rebind('Gameplay', 'Jump', 7, 'Key.J')", "Gameplay/Jump") && h.fails("input.rebind('Gameplay', 'Jump', 1, 'Key.Nope')", "Key.Nope"),
					"lua_rebind: a bad index or control is an error");
				check(h.run("return input.reset('Gameplay', 'Jump')") == "true" && !h.Module.actions().modified("Gameplay", "Jump"), "lua_rebind: reset");
				h.run("input.reset_all()");
				check(!h.Module.actions().modified("Gameplay", "Move"), "lua_rebind: reset_all");
			}

			{
				LuaInputHarness h;
				h.run("count = 0; last = ''; input.listen(function(c) count = count + 1; last = c end)");
				check(h.Module.actions().listening(), "lua_listen: listening after the call");
				check(h.run("return input.listening()") == "true", "lua_listen: input.listening() is true while it waits");
				h.frame();
				check(h.run("return count") == "0", "lua_listen: nothing without input");
				h.Backend.key(Key::E, true);
				h.frame();
				h.Backend.key(Key::F, true);
				h.frame();
				check(h.run("return count .. ':' .. last") == "1:Key.E" && !h.Module.actions().listening(), "lua_listen: the callback gets the control once");

				h.run("count = 0; input.listen(function(c) count = count + 1 end)");
				h.Backend.key(Key::Escape, true);
				h.frame();
				check(h.run("return count") == "0" && !h.Module.actions().listening(), "lua_listen: Escape cancels without a call");
				check(h.run("return input.listening()") == "false", "lua_listen: input.listening() is false after Escape");
				h.run("input.listen(function(c) count = count + 1 end); input.cancel_listen()");
				check(!h.Module.actions().listening(), "lua_listen: cancel_listen");
			}

			{
				const std::filesystem::path path = std::filesystem::temp_directory_path() / "wiwa_input_lua_save.json";
				std::error_code error;
				std::filesystem::remove(path, error);

				LuaInputHarness h;
				h.Module.set_overrides_path(path);
				h.run("input.rebind('Gameplay', 'Jump', 1, 'Key.J')");
				check(h.run("return input.save_bindings()") == "true" && std::filesystem::exists(path), "lua_save: the file is written");

				InputModule fresh;
				fresh.add_maps(action_test_maps());
				fresh.set_overrides_path(path);
				check(fresh.load_overrides() && fresh.actions().action("Gameplay", "Jump")->Bindings[0].Parts[0] == control(Key::J), "lua_save: a fresh module sees the saved binding");
				std::filesystem::remove(path, error);
			}

			{
				LuaInputHarness h;
				h.Instances.context().Input = nullptr;
				check(h.fails("return input.down('Key.W')", "input module") && h.fails("return input.action('Gameplay', 'Jump')", "input module"), "lua_no_module: the calls are errors");
				check(h.run("return 1 + 1") == "2", "lua_no_module: scripts that never touch input are unaffected");
			}

			{
				LuaInputHarness h;
				h.Backend.key(Key::Space, true);
				h.Backend.key(Key::W, true);
				h.Backend.connect_pad(0, true);
				h.Backend.pad_axis(0, PadAxis::LeftX, 0.8f);
				h.frame();
				h.Instances.context().GamePlaying = false;
				check(h.run("return input.down('Key.Space')") == "false" && h.run("return input.pressed('Key.Space')") == "false" && h.run("return input.value('Pad.LeftX')") == "0.0" &&
					h.run("return input.action('Gameplay', 'Jump'):down()") == "false" && h.run("return input.action('Gameplay', 'Move'):vector().y") == "0.0",
					"lua_idle_clock: queries read false / 0 while the game clock is not playing");
				h.Instances.context().GamePlaying = true;
				check(h.run("return input.down('Key.Space')") == "true", "lua_idle_clock: and read again once it plays");
			}
		}

		void action_button_direction(Check& check)
		{
			ActionMap dir;
			dir.Name = "Dir";
			dir.Actions.push_back({ "Up", ActionType::Button, { pad_control(PadAxis::LeftY) } });
			dir.Actions.push_back({ "Down", ActionType::Button, { pad_control(PadAxis::LeftY, 1.0f, 0.0f, true) } });

			ActionRig rig;
			rig.Actions.set_maps({ dir });
			rig.Actions.push_map("Dir");
			rig.Backend.connect_pad(0, true);
			rig.Backend.pad_axis(0, PadAxis::LeftY, 0.8f);
			rig.frame();
			check(rig.Actions.state("Dir", "Up").Down && !rig.Actions.state("Dir", "Down").Down, "act_button_direction: a stick pushed up presses Up only");
			rig.Backend.pad_axis(0, PadAxis::LeftY, -0.8f);
			rig.frame();
			check(!rig.Actions.state("Dir", "Up").Down && rig.Actions.state("Dir", "Down").Down, "act_button_direction: a stick pushed down presses Down only (invert)");
		}

		struct UiRig
		{
			InputModule Module;
			ScriptedInputBackend Backend;

			UiRig()
			{
				Module.set_backend(&Backend);
				Module.add_ui_nav_map();
			}

			UiInputFrame poll()
			{
				Module.update(0.016f);
				return Module.ui_source().poll();
			}
		};

		void ui_input_source(Check& check)
		{
			{
				UiRig rig;
				rig.Backend.move_cursor({ 100.0f, 50.0f });
				rig.Backend.mouse_button(MouseButton::Left, true);
				UiInputFrame frame = rig.poll();
				check(frame.PointerValid && near_equal(frame.Pointer.x, 100.0f) && near_equal(frame.Pointer.y, 50.0f), "ui_pointer: the cursor is the pointer");
				check(frame.Down[0] && frame.Pressed[0] && !frame.Released[0] && !frame.Down[1], "ui_pointer: the left button presses");
				frame = rig.poll();
				check(frame.Down[0] && !frame.Pressed[0], "ui_pointer: held");
				rig.Backend.mouse_button(MouseButton::Left, false);
				frame = rig.poll();
				check(!frame.Down[0] && frame.Released[0], "ui_pointer: released");
				rig.Backend.mouse_button(MouseButton::Right, true);
				rig.Backend.mouse_button(MouseButton::Middle, true);
				rig.Backend.scroll({ 0.0f, 3.0f });
				frame = rig.poll();
				check(frame.Down[1] && frame.Down[2] && near_equal(frame.Scroll, 3.0f), "ui_pointer: right and middle buttons and the scroll");
			}

			{
				UiRig rig;
				rig.Backend.move_cursor({ 100.0f, 50.0f });
				rig.Module.ui_source().set_viewport({ 0.0f, 0.0f }, 1.0f, false);
				check(!rig.poll().PointerValid, "ui_pointer_outside: not hovered gives no pointer");
			}

			{
				UiRig rig;
				rig.Backend.move_cursor({ 60.0f, 70.0f });
				rig.Module.ui_source().set_viewport({ 10.0f, 20.0f }, 2.0f, true);
				const UiInputFrame frame = rig.poll();
				check(frame.PointerValid && near_equal(frame.Pointer.x, 100.0f) && near_equal(frame.Pointer.y, 100.0f), "ui_viewport_origin: (cursor - origin) * scale");
			}

			{
				UiRig rig;
				rig.Backend.key(Key::Enter, true);
				UiInputFrame frame = rig.poll();
				check((frame.NavPressed & ui_nav_bit(UiNav::Submit)) != 0 && (frame.NavPressed & ui_nav_bit(UiNav::Up)) == 0, "ui_nav_keyboard: Enter submits");
				frame = rig.poll();
				check(frame.NavPressed == 0, "ui_nav_keyboard: a held key navigates once");
				rig.Backend.key(Key::Enter, false);
				rig.Backend.key(Key::Up, true);
				rig.Backend.key(Key::Escape, true);
				frame = rig.poll();
				check((frame.NavPressed & ui_nav_bit(UiNav::Up)) != 0 && (frame.NavPressed & ui_nav_bit(UiNav::Cancel)) != 0, "ui_nav_keyboard: arrows and Escape");
			}

			{
				UiRig rig;
				rig.Backend.connect_pad(0, true);
				rig.Backend.pad_button(0, PadButton::DPadDown, true);
				UiInputFrame frame = rig.poll();
				check((frame.NavPressed & ui_nav_bit(UiNav::Down)) != 0, "ui_nav_pad: the d-pad moves down");
				frame = rig.poll();
				check(frame.NavPressed == 0, "ui_nav_pad: once");
				rig.Backend.pad_button(0, PadButton::DPadDown, false);
				rig.poll();
				rig.Backend.pad_axis(0, PadAxis::LeftY, -0.6f);
				frame = rig.poll();
				check((frame.NavPressed & ui_nav_bit(UiNav::Down)) != 0, "ui_nav_pad: a stick at 0.6 pushed down moves down");
				rig.Backend.pad_axis(0, PadAxis::LeftY, 0.0f);
				rig.Backend.pad_axis(0, PadAxis::LeftX, 0.3f);
				frame = rig.poll();
				check(frame.NavPressed == 0, "ui_nav_pad: a small stick movement does nothing");
				rig.Backend.pad_axis(0, PadAxis::LeftX, 0.0f);
				rig.Backend.pad_button(0, PadButton::A, true);
				rig.Backend.pad_button(0, PadButton::B, true);
				frame = rig.poll();
				check((frame.NavPressed & ui_nav_bit(UiNav::Submit)) != 0 && (frame.NavPressed & ui_nav_bit(UiNav::Cancel)) != 0, "ui_nav_pad: A submits and B cancels");
			}

			{
				UiRig rig;
				check(rig.Module.actions().rebind("UI", "Submit", 0, control(Key::Space)), "ui_nav_rebind: the nav bindings can be rebound");
				rig.Backend.key(Key::Space, true);
				UiInputFrame frame = rig.poll();
				check((frame.NavPressed & ui_nav_bit(UiNav::Submit)) != 0, "ui_nav_rebind: Space submits");
				rig.Backend.key(Key::Space, false);
				rig.Backend.key(Key::Enter, true);
				frame = rig.poll();
				check((frame.NavPressed & ui_nav_bit(UiNav::Submit)) == 0, "ui_nav_rebind: Enter no longer does");
			}

			{
				UiRig rig;
				rig.Module.set_capture(false);
				rig.Backend.move_cursor({ 100.0f, 50.0f });
				rig.Backend.mouse_button(MouseButton::Left, true);
				rig.Backend.scroll({ 0.0f, 2.0f });
				rig.Backend.key(Key::Enter, true);
				rig.Backend.connect_pad(0, true);
				rig.Backend.pad_button(0, PadButton::DPadDown, true);
				const UiInputFrame frame = rig.poll();
				check(!frame.PointerValid && !frame.Down[0] && !frame.Pressed[0] && near_equal(frame.Scroll, 0.0f), "ui_capture_off: no pointer, buttons or scroll");
				check((frame.NavPressed & ui_nav_bit(UiNav::Submit)) == 0 && (frame.NavPressed & ui_nav_bit(UiNav::Down)) != 0, "ui_capture_off: the keyboard is muted but the pad still navigates");
			}

			{
				InputModule* module = Engine::get().get_module<InputModule>();
				check(module && module->actions().find_map("UI") != nullptr && module->actions().map_active("UI"), "ui_game_wiring: the engine's input module has the UI nav map active");
				check(module && module->actions().has_action("UI", "NavUp") && module->actions().has_action("UI", "Submit") && module->actions().has_action("UI", "Cancel"), "ui_game_wiring: with its nav actions");
			}
		}

		// How many undo steps the model has (counted on a copy).
		size_t undo_depth(InputEditModel model)
		{
			size_t depth = 0;
			while (model.can_undo())
			{
				model.undo();
				depth++;
			}
			return depth;
		}

		void edit_model(Check& check)
		{
			// every kind of edit is one undo step that undo and redo restore exactly
			{
				InputEditModel model{ InputMapData{} };
				std::vector<std::string> states = { input_map_to_text(model.data()) };
				const auto step = [&]() { states.push_back(input_map_to_text(model.data())); };

				check(model.add_map("Gameplay") == 0, "edit_add_remove: add_map"); step();
				check(model.add_action(0, "Jump", ActionType::Button) == 0, "edit_add_remove: add_action"); step();
				check(model.add_binding(0, 0, control_binding(Key::Space)) == 0, "edit_add_remove: add_binding"); step();
				check(model.add_action(0, "Move", ActionType::Axis2D) == 1, "edit_add_remove: a second action"); step();
				check(model.add_binding(0, 1, composite2d(Key::W, Key::S, Key::A, Key::D)) == 0, "edit_add_remove: a composite"); step();
				check(model.set_binding_part(0, 1, 0, 0, control(Key::I)), "edit_add_remove: set_binding_part"); step();
				check(model.set_binding_params(0, 0, 0, 2.0f, 0.1f, true), "edit_add_remove: set_binding_params"); step();
				check(model.set_consume(0, true), "edit_add_remove: set_consume"); step();
				check(model.rename_action(0, 0, "Hop"), "edit_add_remove: rename_action"); step();
				check(model.rename_map(0, "Main"), "edit_add_remove: rename_map"); step();
				check(model.set_action_type(0, 0, ActionType::Axis1D), "edit_add_remove: set_action_type"); step();
				check(model.remove_binding(0, 1, 0), "edit_add_remove: remove_binding"); step();
				check(model.remove_action(0, 1), "edit_add_remove: remove_action"); step();
				check(model.remove_map(0), "edit_add_remove: remove_map"); step();

				check(undo_depth(model) == states.size() - 1, "edit_add_remove: every edit is one undo step");
				for (size_t i = states.size() - 1; i > 0; i--)
				{
					model.undo();
					check(input_map_to_text(model.data()) == states[i - 1], "edit_add_remove: undo restores step " + std::to_string(i));
				}
				check(!model.can_undo() && model.can_redo(), "edit_add_remove: back at the start");
				for (size_t i = 1; i < states.size(); i++)
				{
					model.redo();
					check(input_map_to_text(model.data()) == states[i], "edit_add_remove: redo restores step " + std::to_string(i));
				}
				check(!model.can_redo(), "edit_add_remove: nothing left to redo");
			}

			// refused calls change nothing and make no step
			{
				InputEditModel model{ InputMapData{} };
				model.add_map("Gameplay");
				model.add_map("UI");
				model.add_action(0, "Jump", ActionType::Button);
				model.add_action(0, "Move", ActionType::Axis2D);
				model.add_binding(0, 0, control_binding(Key::Space));
				const std::string before = input_map_to_text(model.data());
				const size_t depth = undo_depth(model);

				const std::vector<std::pair<std::string, bool>> refused =
				{
					{ "add_map duplicate", model.add_map("Gameplay") != SIZE_MAX },
					{ "add_map empty", model.add_map("") != SIZE_MAX },
					{ "add_map leading space", model.add_map(" Menu") != SIZE_MAX },
					{ "add_map trailing space", model.add_map("Menu ") != SIZE_MAX },
					{ "rename_map duplicate", model.rename_map(1, "Gameplay") },
					{ "rename_map empty", model.rename_map(1, "") },
					{ "rename_map out of range", model.rename_map(9, "X") },
					{ "add_action duplicate", model.add_action(0, "Jump", ActionType::Button) != SIZE_MAX },
					{ "add_action empty", model.add_action(0, "", ActionType::Button) != SIZE_MAX },
					{ "add_action bad map", model.add_action(7, "X", ActionType::Button) != SIZE_MAX },
					{ "rename_action duplicate", model.rename_action(0, 1, "Jump") },
					{ "add_binding wrong kind", model.add_binding(0, 0, composite1d(Key::A, Key::D)) != SIZE_MAX },
					{ "add_binding to Axis2D control", model.add_binding(0, 1, control_binding(Key::A)) != SIZE_MAX },
					{ "add_binding bad action", model.add_binding(0, 9, control_binding(Key::A)) != SIZE_MAX },
					{ "set_binding_params deadzone 1", model.set_binding_params(0, 0, 0, 1.0f, 1.0f, false) },
					{ "set_binding_params negative deadzone", model.set_binding_params(0, 0, 0, 1.0f, -0.1f, false) },
					{ "set_binding_params NaN scale", model.set_binding_params(0, 0, 0, std::numeric_limits<float>::quiet_NaN(), 0.0f, false) },
					{ "set_binding_part bad part", model.set_binding_part(0, 0, 0, 1, control(Key::E)) },
					{ "set_binding_part bad binding", model.set_binding_part(0, 0, 4, 0, control(Key::E)) },
					{ "set_binding_part bad control", model.set_binding_part(0, 0, 0, 0, InputControl{ InputDevice::Key, 9999 }) },
					{ "remove_map out of range", model.remove_map(5) },
					{ "remove_action out of range", model.remove_action(0, 5) },
					{ "remove_binding out of range", model.remove_binding(0, 0, 5) },
					{ "set_consume bad map", model.set_consume(5, true) },
				};
				for (const auto& [name, accepted] : refused)
				{
					check(!accepted, "edit_refused: " + name);
				}
				check(input_map_to_text(model.data()) == before && undo_depth(model) == depth, "edit_refused: nothing changed and no step was made");
			}

			// setting a value to itself is not a step
			{
				InputEditModel model{ sample_map_data() };
				check(!model.can_undo(), "edit_same_value_no_step: a fresh model has no history");
				check(model.set_consume(1, true) && model.rename_map(0, "Gameplay") && model.rename_action(0, 0, "Jump") && model.set_action_type(0, 0, ActionType::Button) &&
					model.set_binding_params(0, 0, 0, 1.0f, 0.0f, false) && model.set_binding_part(0, 0, 0, 0, control(Key::Space)),
					"edit_same_value_no_step: the calls are accepted");
				check(!model.can_undo() && !model.dirty(), "edit_same_value_no_step: and make no step");
			}

			// a type change that would leave a binding the type cannot use is refused
			{
				InputEditModel model{ sample_map_data() };
				check(!model.set_action_type(0, 2, ActionType::Button), "edit_type_change_refused: Axis2D with composite bindings to Button");
				check(!model.set_action_type(0, 1, ActionType::Button), "edit_type_change_refused: Axis1D with a composite to Button");
				check(model.remove_binding(0, 1, 0) && model.set_action_type(0, 1, ActionType::Button), "edit_type_change_refused: allowed once the composite is gone");
				check(model.set_action_type(0, 0, ActionType::Axis1D), "edit_type_change_refused: Button with a control to Axis1D is fine");
			}

			{
				InputEditModel model{ sample_map_data() };
				check(!model.dirty(), "edit_dirty: clean at the start");
				model.add_map("Extra");
				check(model.dirty(), "edit_dirty: an edit makes it dirty");
				model.mark_saved();
				check(!model.dirty(), "edit_dirty: saved is clean");
				model.rename_map(2, "Extra2");
				check(model.dirty(), "edit_dirty: edit after saving");
				model.undo();
				check(!model.dirty(), "edit_dirty: undo back to the saved state is clean");
				model.undo();
				check(model.dirty(), "edit_dirty: undo past it is dirty");
				model.redo();
				check(!model.dirty(), "edit_dirty: redo to the saved state is clean");
			}

			{
				InputEditModel model{ InputMapData{} };
				model.add_map("A");
				model.add_action(0, "Fire", ActionType::Button);
				model.add_binding(0, 0, control_binding(Key::F));
				model.add_action(0, "Aim", ActionType::Axis2D);
				model.add_binding(0, 1, pair2d(PadAxis::RightX, PadAxis::RightY, 0.25f));
				model.add_binding(0, 1, composite2d(Key::I, Key::K, Key::J, Key::L));
				model.rename_action(0, 0, "Shoot");
				model.set_binding_params(0, 0, 0, 0.5f, 0.2f, true);
				model.add_map("B");
				model.remove_map(0);
				std::string error;
				InputMapData parsed;
				check(input_map_valid(model.data(), error) && error.empty(), "edit_output_valid: the document is valid after a series of edits (" + error + ")");
				check(input_map_from_text(input_map_to_text(model.data()), parsed, error) && same_map_data(parsed, model.data()), "edit_output_valid: and survives the file format");
			}

			{
				InputEditModel model{ InputMapData{} };
				for (int i = 0; i < 101; i++)
				{
					model.add_map("M" + std::to_string(i));
				}
				check(undo_depth(model) == InputEditModel::MAX_UNDO_STEPS, "edit_undo_limit: 101 steps keep 100");
				for (size_t i = 0; i < InputEditModel::MAX_UNDO_STEPS; i++)
				{
					model.undo();
				}
				check(!model.can_undo() && model.data().Maps.size() == 1, "edit_undo_limit: the oldest step is gone");
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

		Check check;
		raw_input(check);
		input_module(check);
		input_map_format(check);
		action_evaluation(check);
		action_rebinding(check);
		action_listen(check);
		action_button_direction(check);
		last_device(check);
		input_persistence(check, pipeline);
		input_importer(check, pipeline);
		lua_input(check);
		ui_input_source(check);
		edit_model(check);

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
