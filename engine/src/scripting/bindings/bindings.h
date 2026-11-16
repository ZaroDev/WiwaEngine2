#pragma once

#include <cmath>
#include <string>

#include <sol/sol.hpp>

#include <scene/reflection/field_descriptor.h>
#include "../script_context.h"
#include "../script_runtime.h"
#include "script_types.h"

// One function per area of the Lua API; each registers its part in the state of a ScriptRuntime (types on the state, functions in
// the sandbox the scripts run in).
namespace wiwa
{
	// The module a binding needs, or a Lua error with the module's own message where the host has none (a test).
	template<typename T>
	T& require_module(T* module, const char* message)
	{
		if (module == nullptr)
		{
			throw sol::error(message);
		}
		return *module;
	}

	// Arguments the scripting state cannot type-check (no sol2 safety defines): refused here with the area and function in the message.
	// A vec3 argument. The scripting state has no sol2 type checks, so a number or a table where a vector goes must be refused here, not read as one.
	inline glm::vec3 vec3_arg(const char* area, const sol::object& value, const char* function, const char* argument)
	{
		if (!value.is<LuaVec3>())
		{
			throw sol::error(std::string(area) + "." + function + ": " + argument + " must be a vec3");
		}
		const glm::vec3 vector = value.as<LuaVec3&>().Value;
		if (!std::isfinite(vector.x) || !std::isfinite(vector.y) || !std::isfinite(vector.z))
		{
			throw sol::error(std::string(area) + "." + function + ": " + argument + " must be finite numbers (got " + std::to_string(vector.x) + ", " + std::to_string(vector.y) + ", " + std::to_string(vector.z) + ")");
		}
		return vector;
	}

	inline float number_arg(const char* area, const sol::object& value, const char* function, const char* argument)
	{
		if (!value.is<double>() || !std::isfinite(value.as<double>()))
		{
			throw sol::error(std::string(area) + "." + function + ": " + argument + " must be a finite number");
		}
		return static_cast<float>(value.as<double>());
	}

	// vec2, vec3, vec4, quat, mat4.
	void bind_math(ScriptRuntime& runtime);

	// Entity, Component, and create_entity / destroy_entity / find_entity.
	void bind_scene(ScriptRuntime& runtime, ScriptContext& context);

	// load_asset, load_scene.
	void bind_assets(ScriptRuntime& runtime, ScriptContext& context);

	// debug_draw.line / box / sphere / arrow, camera.primary / set_primary.
	void bind_render(ScriptRuntime& runtime, ScriptContext& context);

	// time.delta / time.total, and quit.
	void bind_engine(ScriptRuntime& runtime, ScriptContext& context);

	// physics.raycast / raycast_all / overlap_sphere / overlap_box / shape_cast / set_gravity, and entity:body() with its Body methods.
	void bind_physics(ScriptRuntime& runtime, ScriptContext& context);

	// input.down / pressed / released / value / mouse / mouse_delta / scroll / pad_connected / last_device / set_cursor, input.action (a handle with
	// down / pressed / released / value / vector), push_map / pop_map, and the rebinding calls (bindings, rebind, reset, listen, save_bindings).
	void bind_input(ScriptRuntime& runtime, ScriptContext& context);

	// Audio.post(event [, entity]) (an AudioHandle, or nil when the event did not start), stop, stop_entity, stop_all, set_rtpc, set_switch, set_state,
	// load_bank, backend.
	void bind_audio(ScriptRuntime& runtime, ScriptContext& context);

	// UI.panel(entity), panels and their widgets (properties, on / off), UI.input.
	void bind_ui(ScriptRuntime& runtime, ScriptContext& context);

	// Nav.agent(entity) and its handle (set_destination, stop, warp, state, remaining_distance, velocity), Nav.find_path / nearest_point / raycast / random_point.
	void bind_nav(ScriptRuntime& runtime, ScriptContext& context);

	// AI.controller(entity) and its handle (get, set, restart, running), AI.emit_noise(position, loudness, kind).
	void bind_ai(ScriptRuntime& runtime, ScriptContext& context);

	// Animation.animator(entity), the animator handle (play, crossfade, stop, layer, on, off, joint_world, ...) and layer handles.
	void bind_animation(ScriptRuntime& runtime, ScriptContext& context);

	// Decal.spawn{ material, position, normal, size, opacity, lifetime, fade_in, fade_out, sort_order, roll, parent, name }.
	void bind_decal(ScriptRuntime& runtime, ScriptContext& context);

	// Vfx.spawn{ effect, position, rotation, scale, parent, name, looping, time_scale, params, animator, joint }, Vfx.play / stop / restart / is_playing / set_param.
	void bind_vfx(ScriptRuntime& runtime, ScriptContext& context);

	// settings.graphics.get / set (the renderer's quality settings, saved), settings.scene.get / set (the active scene's look), and
	// prefs.get / set / save (the player's options of a game, user_prefs.json).
	void bind_settings(ScriptRuntime& runtime, ScriptContext& context);

	// A field's value as Lua holds it. A vector or quaternion keeps `link` so that writes to its parts reach the field.
	sol::object field_to_lua(sol::state_view lua, const FieldValue& value, const FieldDescriptor& field, const FieldLink& link);

	// The other way: false, with `error` saying what was expected, if the Lua value is not one the field can hold.
	bool field_from_lua(const FieldDescriptor& field, const sol::object& object, FieldValue& out, std::string& error);
}
