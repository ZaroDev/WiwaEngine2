#include <pch.h>
#include "bindings.h"

#include <cmath>

#include <ai/ai_module.h>
#include <ai/bt_lua_host.h>

namespace wiwa
{
	namespace
	{
		// An AIController as a script holds it: the entity, looked up again on every use.
		struct LuaAiController
		{
			EntityHandle Entity{};
		};

		AIModule& ai_of(ScriptContext& context)
		{
			if (context.AI == nullptr)
			{
				throw sol::error("the AI module is not available");
			}
			return *context.AI;
		}

		// `self` of a handle method: called with a dot it is not a handle, and sol2 (no safety defines) would read it unchecked.
		const LuaAiController& self_of(const sol::object& self)
		{
			if (!self.is<LuaAiController>())
			{
				throw sol::error("call controller methods with a colon: ctrl:get(key)");
			}
			return self.as<LuaAiController&>();
		}

		// The running instance of the handle's controller: the entity must exist and have an enabled AIController with a tree that has loaded.
		BtInstance& instance_of(ScriptContext& context, const sol::object& self)
		{
			const LuaAiController& controller = self_of(self);
			const entt::entity entity = controller.Entity.resolve();
			if (entity == entt::null)
			{
				throw sol::error("the entity no longer exists");
			}
			BtInstance* instance = ai_of(context).acquire(entity);
			if (instance == nullptr)
			{
				throw sol::error("the AIController has no running tree (it is disabled, has no tree, or the tree has not loaded or is not valid)");
			}
			return *instance;
		}

		std::string key_arg(const sol::object& key, const char* function)
		{
			if (key.get_type() != sol::type::string)
			{
				throw sol::error(std::string("AI controller ") + function + ": the key must be a string");
			}
			return key.as<std::string>();
		}
	}

	void bind_ai(ScriptRuntime& runtime, ScriptContext& context)
	{
		sol::state& lua = runtime.state();

		lua.new_usertype<LuaAiController>("AIHandle", sol::no_constructor,
			// A blackboard value: numbers, vec3, string, an Entity (or nil for none). An unknown key is an error naming it.
			"get", [&context](const sol::object& self, const sol::object& key, sol::this_state state) -> sol::object
			{
				BtInstance& instance = instance_of(context, self);
				const std::string name = key_arg(key, "get");
				const std::optional<BtValue> value = instance.blackboard().get(name);
				if (!value)
				{
					throw sol::error("the blackboard has no key \"" + name + "\"");
				}
				return bt_value_to_lua(sol::state_view(state), &context, *value);
			},
			// Sets a value of the key's own type (no conversions; a number for an int key must be whole). Errors name the key and the type.
			"set", [&context](const sol::object& self, const sol::object& key, const sol::object& value)
			{
				BtInstance& instance = instance_of(context, self);
				const std::string name = key_arg(key, "set");
				const std::optional<BtValueType> type = instance.blackboard().type_of(name);
				if (!type)
				{
					throw sol::error("the blackboard has no key \"" + name + "\"");
				}
				BtValue converted;
				std::string why;
				if (!bt_value_from_lua(*type, value, converted, why))
				{
					throw sol::error("the key \"" + name + "\" is " + bt_value_type_name(*type) + ": expected " + why);
				}
				std::string error;
				if (!instance.blackboard().set(name, converted, &error) && !instance.blackboard().set_dynamic(name, converted))
				{
					throw sol::error(error);
				}
			},
			// Aborts what runs and puts the blackboard back to the defaults and the controller's overrides.
			"restart", [&context](const sol::object& self)
			{
				instance_of(context, self); // the same checks
				const LuaAiController& controller = self_of(self);
				ai_of(context).restart(controller.Entity.resolve());
			},
			// Whether the tree is in the middle of a run (some node said Running at the last tick).
			"running", [&context](const sol::object& self) -> bool { return instance_of(context, self).running(); });

		sol::table ai = lua.create_table();

		// The handle of the AIController of an entity (an error when it has none).
		ai.set_function("controller", [&context](const sol::object& entity) -> LuaAiController
			{
				if (!entity.is<EntityHandle>())
				{
					throw sol::error("AI.controller: the argument must be an entity");
				}
				const EntityHandle handle = entity.as<EntityHandle>();
				const entt::entity resolved = handle.resolve();
				if (resolved == entt::null)
				{
					throw sol::error("the entity no longer exists");
				}
				if (context.ActiveScene == nullptr || !context.ActiveScene->registry().all_of<AIController>(resolved))
				{
					throw sol::error("the entity has no AIController component");
				}
				ai_of(context);
				return LuaAiController{ handle };
			});

		// A sound at a place that the AIs with a Perceiver nearby may hear this frame: AI.emit_noise(position, loudness, kind). Loudness scales a
		// perceiver's hearing radius; kind is a text the trees can look at (written to the heard_kind key).
		ai.set_function("emit_noise", [&context](const sol::object& position, const sol::object& loudness, const sol::object& kind)
			{
				if (!position.is<LuaVec3>())
				{
					throw sol::error("AI.emit_noise: the position must be a vec3");
				}
				const glm::vec3 where = position.as<LuaVec3&>().Value;
				if (!std::isfinite(where.x) || !std::isfinite(where.y) || !std::isfinite(where.z))
				{
					throw sol::error("AI.emit_noise: the position must be finite numbers");
				}
				if (loudness.get_type() != sol::type::number || !std::isfinite(loudness.as<double>()) || loudness.as<double>() < 0.0 || loudness.as<double>() > 1000.0)
				{
					throw sol::error("AI.emit_noise: the loudness must be a number from 0 to 1000");
				}
				if (kind.get_type() != sol::type::lua_nil && kind.get_type() != sol::type::string && kind.get_type() != sol::type::none)
				{
					throw sol::error("AI.emit_noise: the kind must be a string");
				}
				ai_of(context).emit_noise(where, static_cast<float>(loudness.as<double>()), kind.get_type() == sol::type::string ? kind.as<std::string>() : std::string());
			});

		runtime.sandbox()["AI"] = ai;
	}
}
