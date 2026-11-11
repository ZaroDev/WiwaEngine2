#pragma once

#include <sol/sol.hpp>

#include <scene/reflection/field_descriptor.h>
#include "../script_context.h"
#include "../script_runtime.h"
#include "script_types.h"

// One function per area of the Lua API; each registers its part in the state of a ScriptRuntime (types on the state, functions in
// the sandbox the scripts run in).
namespace wiwa
{
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
