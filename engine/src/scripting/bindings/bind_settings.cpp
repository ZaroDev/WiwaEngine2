#include <pch.h>
#include "bindings.h"

#include <core/engine.h>
#include <core/user_prefs.h>
#include <renderer/render_module.h>

namespace wiwa
{
	namespace
	{
		RenderModule& render_module()
		{
			RenderModule* render = Engine::get().get_module<RenderModule>();
			if (render == nullptr)
			{
				throw sol::error("there is no renderer here");
			}
			return *render;
		}

		sol::table quality_to_lua(sol::state_view lua, const RenderQuality& quality)
		{
			sol::table table = lua.create_table();
			table["vsync"] = quality.Vsync;
			table["shadow_distance"] = quality.ShadowDistance;
			table["shadow_cascades"] = quality.ShadowCascades;
			table["bloom_mips"] = quality.BloomMips;
			table["ao_slices"] = quality.AoSlices;
			table["ao_steps"] = quality.AoSteps;
			table["fxaa"] = quality.Fxaa;
			table["taa"] = quality.Taa;
			return table;
		}

		bool read_bool(const std::string& key, const sol::object& value)
		{
			if (value.get_type() != sol::type::boolean)
			{
				throw sol::error("settings: '" + key + "' is a boolean");
			}
			return value.as<bool>();
		}

		float read_number(const std::string& key, const sol::object& value)
		{
			if (value.get_type() != sol::type::number)
			{
				throw sol::error("settings: '" + key + "' is a number");
			}
			return value.as<float>();
		}

		// Merges the keys of the table into the quality; an unknown key or a value of the wrong type is an error that names it.
		void merge_quality(RenderQuality& quality, const sol::table& table)
		{
			for (const auto& entry : table)
			{
				if (entry.first.get_type() != sol::type::string)
				{
					throw sol::error("settings.graphics.set takes a table with names as keys");
				}
				const std::string key = entry.first.as<std::string>();
				if (key == "vsync") quality.Vsync = read_bool(key, entry.second);
				else if (key == "shadow_distance") quality.ShadowDistance = read_number(key, entry.second);
				else if (key == "shadow_cascades") quality.ShadowCascades = static_cast<u32>(std::max(0.0f, read_number(key, entry.second)));
				else if (key == "bloom_mips") quality.BloomMips = static_cast<u32>(std::max(0.0f, read_number(key, entry.second)));
				else if (key == "ao_slices") quality.AoSlices = static_cast<u32>(std::max(0.0f, read_number(key, entry.second)));
				else if (key == "ao_steps") quality.AoSteps = static_cast<u32>(std::max(0.0f, read_number(key, entry.second)));
				else if (key == "fxaa") quality.Fxaa = read_bool(key, entry.second);
				else if (key == "taa") quality.Taa = read_bool(key, entry.second);
				else throw sol::error("settings.graphics: unknown setting '" + key + "'");
			}
		}

		Scene& active_scene(ScriptContext& context)
		{
			if (context.ActiveScene == nullptr)
			{
				throw sol::error("there is no active scene");
			}
			return *context.ActiveScene;
		}
	}

	void bind_settings(ScriptRuntime& runtime, ScriptContext& context)
	{
		sol::state_view lua = runtime.state();
		sol::table settings = lua.create_table();

		// the renderer's quality settings of the project: applied at once and saved to render_quality.json
		sol::table graphics = lua.create_table();
		graphics.set_function("get", [](sol::this_state state)
			{
				return quality_to_lua(sol::state_view(state), render_module().get_quality());
			});
		graphics.set_function("set", [](const sol::table& table)
			{
				RenderModule& render = render_module();
				RenderQuality quality = render.get_quality();
				merge_quality(quality, table);
				render.set_quality(quality);
			});
		settings["graphics"] = graphics;

		// how the active scene is shown: applied at once, not saved (the scene file is the default)
		sol::table scene = lua.create_table();
		scene.set_function("get", [&context](sol::this_state state)
			{
				const SceneSettings& current = active_scene(context).settings();
				sol::table table = sol::state_view(state).create_table();
				table["bloom"] = current.Bloom.Enabled;
				table["ao"] = current.Ao.Enabled;
				table["motion_blur"] = current.MotionBlur.Enabled;
				table["exposure_compensation"] = current.Exposure.CompensationEv;
				return table;
			});
		scene.set_function("set", [&context](const sol::table& table)
			{
				Scene& active = active_scene(context);
				SceneSettings next = active.settings();
				for (const auto& entry : table)
				{
					if (entry.first.get_type() != sol::type::string)
					{
						throw sol::error("settings.scene.set takes a table with names as keys");
					}
					const std::string key = entry.first.as<std::string>();
					if (key == "bloom") next.Bloom.Enabled = read_bool(key, entry.second);
					else if (key == "ao") next.Ao.Enabled = read_bool(key, entry.second);
					else if (key == "motion_blur") next.MotionBlur.Enabled = read_bool(key, entry.second);
					else if (key == "exposure_compensation") next.Exposure.CompensationEv = read_number(key, entry.second);
					else throw sol::error("settings.scene: unknown setting '" + key + "'");
				}
				active.set_settings(next);
			});
		settings["scene"] = scene;
		runtime.sandbox()["settings"] = settings;

		// the player's own options of this game, kept in user_prefs.json
		sol::table prefs = lua.create_table();
		prefs.set_function("get", [](const std::string& key, sol::object fallback, sol::this_state state) -> sol::object
			{
				const std::optional<UserPrefs::Value> value = UserPrefs::get().value(key);
				if (!value)
				{
					return fallback;
				}
				sol::state_view lua(state);
				if (const bool* flag = std::get_if<bool>(&*value))
				{
					return sol::make_object(lua, *flag);
				}
				if (const double* number = std::get_if<double>(&*value))
				{
					return sol::make_object(lua, *number);
				}
				return sol::make_object(lua, std::get<std::string>(*value));
			});
		prefs.set_function("set", [](const std::string& key, const sol::object& value)
			{
				switch (value.get_type())
				{
				case sol::type::boolean: UserPrefs::get().set(key, value.as<bool>()); break;
				case sol::type::number: UserPrefs::get().set(key, value.as<double>()); break;
				case sol::type::string: UserPrefs::get().set(key, value.as<std::string>()); break;
				default: throw sol::error("prefs.set: '" + key + "' takes a boolean, a number or a string");
				}
			});
		prefs.set_function("save", []() { return UserPrefs::get().save(); });
		runtime.sandbox()["prefs"] = prefs;
	}
}
