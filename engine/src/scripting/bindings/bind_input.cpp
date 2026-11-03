#include <pch.h>
#include "bindings.h"

#include <input/input_module.h>

#define WI_SCRIPT_LOG(level, ...) WI_LOG(level, "Scripting", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		// An action as a script holds it: input.action("Gameplay", "Jump"). It reads the state of the current frame on every call.
		struct LuaInputAction
		{
			ScriptContext* Context = nullptr;
			std::string Map{};
			std::string Action{};
		};

		InputModule& input_of(ScriptContext& context)
		{
			if (context.Input == nullptr)
			{
				throw sol::error("the input module is not available");
			}
			return *context.Input;
		}

		// Queries only mean something while the game clock plays; outside it they read false / zero.
		bool live(ScriptContext& context)
		{
			return context.GamePlaying;
		}

		InputControl control_of(const std::string& name)
		{
			const std::optional<InputControl> parsed = parse_control(name);
			if (!parsed)
			{
				throw sol::error("unknown input control '" + name + "' (expected a name like 'Key.W', 'Mouse.Left' or 'Pad.A')");
			}
			return *parsed;
		}

		std::string action_name(const std::string& map, const std::string& action)
		{
			return map + "/" + action;
		}

		const char* kind_name(Binding::Kind kind)
		{
			switch (kind)
			{
			case Binding::Kind::Control: return "control";
			case Binding::Kind::Composite1D: return "composite1d";
			case Binding::Kind::Composite2D: return "composite2d";
			case Binding::Kind::Pair2D: return "pair2d";
			}
			return "";
		}

		ActionState state_of(const LuaInputAction& handle)
		{
			ScriptContext& context = *handle.Context;
			InputModule& input = input_of(context);
			return live(context) ? input.actions().state(handle.Map, handle.Action) : ActionState{};
		}

		LuaVec2 vec2_of(glm::vec2 value)
		{
			return LuaVec2{ value, {} };
		}
	}

	void bind_input(ScriptRuntime& runtime, ScriptContext& context)
	{
		sol::state& lua = runtime.state();

		lua.new_usertype<LuaInputAction>("InputAction", sol::no_constructor,
			"down", [](const LuaInputAction& handle) { return state_of(handle).Down; },
			"pressed", [](const LuaInputAction& handle) { return state_of(handle).Pressed; },
			"released", [](const LuaInputAction& handle) { return state_of(handle).Released; },
			"value", [](const LuaInputAction& handle) { return state_of(handle).Value; },
			"vector", [](const LuaInputAction& handle) { return vec2_of(state_of(handle).Vector); });

		sol::table input = lua.create_table();

		// Raw queries. `pad` is the gamepad index, 0-based; without it a Pad control reads the largest of all connected pads.
		input.set_function("down", [&context](const std::string& name, sol::optional<int> pad)
			{
				const InputControl control = control_of(name);
				InputModule& module = input_of(context);
				return live(context) && module.state().down(control, pad.value_or(-1));
			});
		input.set_function("pressed", [&context](const std::string& name, sol::optional<int> pad)
			{
				const InputControl control = control_of(name);
				InputModule& module = input_of(context);
				return live(context) && module.state().pressed(control, pad.value_or(-1));
			});
		input.set_function("released", [&context](const std::string& name, sol::optional<int> pad)
			{
				const InputControl control = control_of(name);
				InputModule& module = input_of(context);
				return live(context) && module.state().released(control, pad.value_or(-1));
			});
		input.set_function("value", [&context](const std::string& name, sol::optional<int> pad)
			{
				const InputControl control = control_of(name);
				InputModule& module = input_of(context);
				return live(context) ? module.state().value(control, pad.value_or(-1)) : 0.0f;
			});
		input.set_function("mouse", [&context]() { return vec2_of(live(context) ? input_of(context).state().mouse_position() : glm::vec2(0.0f)); });
		input.set_function("mouse_delta", [&context]() { return vec2_of(live(context) ? input_of(context).state().mouse_delta() : glm::vec2(0.0f)); });
		input.set_function("scroll", [&context]() { return vec2_of(live(context) ? input_of(context).state().scroll() : glm::vec2(0.0f)); });
		input.set_function("pad_connected", [&context](int pad) { return input_of(context).state().pad_connected(static_cast<u32>(std::max(pad, 0))) && pad >= 0; });
		input.set_function("last_device", [&context]()
			{
				return input_of(context).state().last_device() == LastDevice::Gamepad ? "gamepad" : "keyboard_mouse";
			});
		input.set_function("set_cursor", [&context](const std::string& mode)
			{
				InputModule& module = input_of(context);
				if (mode == "normal") module.set_cursor_mode(CursorMode::Normal);
				else if (mode == "hidden") module.set_cursor_mode(CursorMode::Hidden);
				else if (mode == "locked") module.set_cursor_mode(CursorMode::Locked);
				else throw sol::error("unknown cursor mode '" + mode + "' (expected 'normal', 'hidden' or 'locked')");
			});

		// Actions.
		input.set_function("action", [&context](const std::string& map, const std::string& action)
			{
				InputModule& module = input_of(context);
				if (!module.actions().has_action(map, action))
				{
					throw sol::error("unknown input action '" + action_name(map, action) + "'");
				}
				return LuaInputAction{ &context, map, action };
			});
		input.set_function("push_map", [&context](const std::string& map)
			{
				InputModule& module = input_of(context);
				if (!module.actions().find_map(map))
				{
					throw sol::error("unknown input map '" + map + "'");
				}
				module.actions().push_map(map);
			});
		input.set_function("pop_map", [&context](const std::string& map)
			{
				InputModule& module = input_of(context);
				if (!module.actions().find_map(map))
				{
					throw sol::error("unknown input map '" + map + "'");
				}
				module.actions().pop_map(map);
			});

		// Rebinding. Binding indices are 1-based like Lua arrays (input.bindings gives the array they index).
		input.set_function("bindings", [&context, &lua](const std::string& map, const std::string& action)
			{
				InputModule& module = input_of(context);
				const Action* found = module.actions().action(map, action);
				if (found == nullptr)
				{
					throw sol::error("unknown input action '" + action_name(map, action) + "'");
				}

				sol::table list = lua.create_table();
				for (const Binding& binding : found->Bindings)
				{
					sol::table entry = lua.create_table();
					sol::table controls = lua.create_table();
					sol::table parts = lua.create_table();
					for (size_t part = 0; part < binding_part_count(binding.BindingKind); part++)
					{
						controls.add(control_name(binding.Parts[part]));
						parts.add(binding_part_name(binding.BindingKind, part));
					}
					entry["kind"] = kind_name(binding.BindingKind);
					entry["controls"] = controls;
					entry["parts"] = parts;
					entry["scale"] = binding.Scale;
					entry["deadzone"] = binding.Deadzone;
					entry["invert"] = binding.Invert;
					list.add(entry);
				}
				return list;
			});
		// Returns true; or false and the list of { map, action, binding } that already use the control. The rebind is applied either way:
		// the script (or the player) decides what to do about a conflict.
		input.set_function("rebind", [&context, &lua](const std::string& map, const std::string& action, int index, const std::string& name, sol::optional<std::string> part) -> sol::variadic_results
			{
				InputModule& module = input_of(context);
				const InputControl control = control_of(name);
				const std::vector<BindingConflict> conflicts = module.actions().conflicts(map, control, action);

				if (index < 1 || !module.actions().rebind(map, action, static_cast<size_t>(index - 1), control, part.value_or(std::string())))
				{
					throw sol::error("cannot rebind '" + action_name(map, action) + "': there is no binding " + std::to_string(index) +
						(part ? " with a part '" + *part + "'" : std::string()));
				}

				sol::variadic_results results;
				if (conflicts.empty())
				{
					results.push_back(sol::make_object(lua, true));
					return results;
				}

				sol::table list = lua.create_table();
				for (const BindingConflict& conflict : conflicts)
				{
					sol::table entry = lua.create_table();
					entry["map"] = conflict.Map;
					entry["action"] = conflict.Action;
					entry["binding"] = conflict.Binding + 1;
					list.add(entry);
				}
				results.push_back(sol::make_object(lua, false));
				results.push_back(sol::make_object(lua, list));
				return results;
			});
		input.set_function("reset", [&context](const std::string& map, const std::string& action)
			{
				return input_of(context).actions().reset_action(map, action);
			});
		input.set_function("reset_all", [&context]() { input_of(context).actions().reset_all(); });
		input.set_function("save_bindings", [&context]() { return input_of(context).save_overrides(); });

		// The callback gets the name of the next control the player presses ("Key.E", "Pad.LeftX"); Escape cancels without calling it.
		// It runs on the main thread, from the input module's update.
		input.set_function("listen", [&context](sol::protected_function callback)
			{
				InputModule& module = input_of(context);
				context.InputListening = true;
				module.actions().listen_for_control([&context, callback](const InputControl& control)
					{
						context.InputListening = false;
						sol::protected_function_result result = callback(control_name(control));
						if (!result.valid())
						{
							sol::error error = result;
							WI_SCRIPT_LOG(Error, "input.listen callback: {0}", error.what());
						}
					});
			});
		input.set_function("cancel_listen", [&context]()
			{
				input_of(context).actions().cancel_listen();
				context.InputListening = false;
			});

		runtime.sandbox()["input"] = input;
	}
}
