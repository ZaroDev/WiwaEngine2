#include <pch.h>
#include "bt_lua_host.h"

#include <cmath>

#include <scene/components.h>
#include <scripting/bindings/bindings.h>
#include <scripting/script_context.h>
#include <scripting/script_runtime.h>

namespace wiwa
{
	namespace
	{
		constexpr const char* BASES = R"lua(
			local Script = ...
			local BtAction = Script:extend{}
			BtAction.__bt_kind = "action"
			local BtCondition = Script:extend{}
			BtCondition.__bt_kind = "condition"
			return BtAction, BtCondition
		)lua";

		// The blackboard as a leaf sees it.
		struct LuaBlackboard
		{
			Blackboard* Board = nullptr;
			ScriptContext* Context = nullptr;
			std::weak_ptr<bool> Alive{};
		};

		LuaBlackboard& board_of(const sol::object& self)
		{
			if (!self.is<LuaBlackboard>())
			{
				throw sol::error("call blackboard methods with a colon: bb:get(key)");
			}
			LuaBlackboard& board = self.as<LuaBlackboard&>();
			const std::shared_ptr<bool> alive = board.Alive.lock();
			if (!alive || !*alive || board.Board == nullptr)
			{
				throw sol::error("the blackboard is gone: its controller has stopped (do not keep bb between calls)");
			}
			return board;
		}

		sol::object bt_value_to_lua_impl(sol::state_view lua, ScriptContext* context, const BtValue& value)
		{
			switch (bt_value_type_of(value))
			{
			case BtValueType::Bool: return sol::make_object(lua, std::get<bool>(value));
			case BtValueType::Int: return sol::make_object(lua, static_cast<lua_Integer>(std::get<i32>(value)));
			case BtValueType::Float: return sol::make_object(lua, static_cast<double>(std::get<float>(value)));
			case BtValueType::Vec3: return sol::make_object(lua, LuaVec3{ std::get<glm::vec3>(value), {} });
			case BtValueType::String: return sol::make_object(lua, std::get<std::string>(value));
			case BtValueType::Entity:
			{
				const EntityGuid guid = std::get<EntityGuid>(value);
				if (!guid.valid())
				{
					return sol::object(lua, sol::lua_nil);
				}
				return sol::make_object(lua, EntityHandle{ guid, context });
			}
			}
			return sol::object(lua, sol::lua_nil);
		}

		// A Lua value as the value of a blackboard key of that type. False, with why, if it is not one.
		bool bt_value_from_lua_impl(BtValueType type, const sol::object& value, BtValue& out, std::string& why)
		{
			switch (type)
			{
			case BtValueType::Bool:
				if (!value.is<bool>()) { why = "a boolean"; return false; }
				out = value.as<bool>();
				return true;
			case BtValueType::Int:
			{
				if (value.get_type() != sol::type::number) { why = "a whole number"; return false; }
				const double number = value.as<double>();
				if (!std::isfinite(number) || number != std::floor(number) || std::abs(number) > 2147483647.0) { why = "a whole number"; return false; }
				out = static_cast<i32>(number);
				return true;
			}
			case BtValueType::Float:
				if (value.get_type() != sol::type::number || !std::isfinite(value.as<double>())) { why = "a number"; return false; }
				out = static_cast<float>(value.as<double>());
				return true;
			case BtValueType::Vec3:
				if (!value.is<LuaVec3>()) { why = "a vec3"; return false; }
				out = value.as<LuaVec3&>().Value;
				return true;
			case BtValueType::String:
				if (value.get_type() != sol::type::string) { why = "a string"; return false; }
				out = value.as<std::string>();
				return true;
			case BtValueType::Entity:
				if (value.get_type() == sol::type::lua_nil) { out = EntityGuid{}; return true; }
				if (!value.is<EntityHandle>()) { why = "an entity or nil"; return false; }
				out = value.as<EntityHandle>().Guid;
				return true;
			}
			return false;
		}

		// A node parameter as the value of a script property of that field type; nullopt if it does not fit.
		std::optional<FieldValue> param_to_field(const FieldDescriptor& field, const nlohmann::json& value)
		{
			switch (field.Type)
			{
			case FieldType::Bool: if (value.is_boolean()) return FieldValue(value.get<bool>()); break;
			case FieldType::Int: if (value.is_number_integer()) return FieldValue(static_cast<i32>(value.get<i64>())); break;
			case FieldType::Float: if (value.is_number()) return FieldValue(value.get<float>()); break;
			case FieldType::String: if (value.is_string()) return FieldValue(value.get<std::string>()); break;
			case FieldType::Vec3:
				if (value.is_array() && value.size() == 3 && value[0].is_number() && value[1].is_number() && value[2].is_number())
				{
					return FieldValue(glm::vec3(value[0].get<float>(), value[1].get<float>(), value[2].get<float>()));
				}
				break;
			default: break;
			}
			return std::nullopt;
		}
	}

	sol::object bt_value_to_lua(sol::state_view lua, ScriptContext* context, const BtValue& value) { return bt_value_to_lua_impl(lua, context, value); }
	bool bt_value_from_lua(BtValueType type, const sol::object& value, BtValue& out, std::string& why) { return bt_value_from_lua_impl(type, value, out, why); }

	void define_bt_lua_bases(ScriptRuntime& runtime)
	{
		sol::table& sandbox = runtime.sandbox();
		if (sandbox.get<sol::object>("BtAction").get_type() != sol::type::lua_nil && sandbox.get<sol::object>("BtCondition").get_type() != sol::type::lua_nil)
		{
			return;
		}
		sol::state& lua = runtime.state();
		// the chunk takes Script as its argument: call it as a function
		sol::protected_function chunk = lua.load(BASES);
		sol::protected_function_result bases = chunk(sandbox["Script"]);
		if (!bases.valid())
		{
			sol::error failure = bases;
			WI_AI_LOG(Error, "Cannot define BtAction and BtCondition: {0}", failure.what());
			return;
		}
		sandbox["BtAction"] = bases.get<sol::object>(0);
		sandbox["BtCondition"] = bases.get<sol::object>(1);

		if (lua.get<sol::object>("BtBlackboard").get_type() == sol::type::lua_nil)
		{
			lua.new_usertype<LuaBlackboard>("BtBlackboard", sol::no_constructor,
				"get", [](const sol::object& self, const sol::object& key, sol::this_state state) -> sol::object
				{
					LuaBlackboard& board = board_of(self);
					if (key.get_type() != sol::type::string)
					{
						throw sol::error("bb:get: the key must be a string");
					}
					const std::string name = key.as<std::string>();
					const std::optional<BtValue> value = board.Board->get(name);
					if (!value)
					{
						throw sol::error("the blackboard has no key \"" + name + "\"");
					}
					return bt_value_to_lua_impl(sol::state_view(state), board.Context, *value);
				},
				"has", [](const sol::object& self, const sol::object& key) -> bool
				{
					LuaBlackboard& board = board_of(self);
					return key.get_type() == sol::type::string && board.Board->has(key.as<std::string>());
				},
				"set", [](const sol::object& self, const sol::object& key, const sol::object& value)
				{
					LuaBlackboard& board = board_of(self);
					if (key.get_type() != sol::type::string)
					{
						throw sol::error("bb:set: the key must be a string");
					}
					const std::string name = key.as<std::string>();
					const std::optional<BtValueType> type = board.Board->type_of(name);
					if (!type)
					{
						throw sol::error("the blackboard has no key \"" + name + "\"");
					}
					BtValue converted;
					std::string why;
					if (!bt_value_from_lua_impl(*type, value, converted, why))
					{
						throw sol::error("the key \"" + name + "\" is " + bt_value_type_name(*type) + ": expected " + why);
					}
					std::string error;
					if (!board.Board->set(name, converted, &error) && !board.Board->set_dynamic(name, converted))
					{
						throw sol::error(error);
					}
				});
		}
	}

	std::string bt_leaf_kind(const sol::table& scriptClass)
	{
		const sol::object kind = scriptClass["__bt_kind"];
		return kind.get_type() == sol::type::string ? kind.as<std::string>() : std::string();
	}

	BtLuaHost::BtLuaHost(ScriptRuntime& runtime, ScriptContext& context) : m_Runtime(runtime), m_Context(context)
	{
		define_bt_lua_bases(runtime);
	}

	bool BtLuaHost::fail(BtLeafCall& call, const std::string& script, const std::string& why)
	{
		if (call.Instance->warn_once(call.NodeIndex))
		{
			WI_AI_LOG(Warning, "Behaviour tree: {0} node {1}{2}: {3}", call.Node->Type, call.Node->Id, script.empty() ? std::string() : " (" + script + ")", why);
		}
		return false;
	}

	sol::object BtLuaHost::blackboard_for(BtInstance& instance)
	{
		sol::state_view lua(m_Runtime.state().lua_state());
		return sol::make_object(lua, LuaBlackboard{ &instance.blackboard(), &m_Context, instance.alive_token() });
	}

	BtStatus BtLuaHost::handle(BtLeafCall& call)
	{
		const Key key{ call.Instance, call.NodeIndex };
		const auto it = m_Leaves.find(key);

		if (call.Phase == BtLeafPhase::Abort)
		{
			if (it != m_Leaves.end())
			{
				abort(call, it->second);
				m_Leaves.erase(it);
			}
			return BtStatus::Failure;
		}
		if (call.Phase == BtLeafPhase::Start)
		{
			if (it != m_Leaves.end())
			{
				m_Leaves.erase(it); // a run that was never ended
			}
			return start(call);
		}
		if (it == m_Leaves.end())
		{
			return BtStatus::Failure;
		}
		return tick(call, it->second);
	}

	BtStatus BtLuaHost::start(BtLeafCall& call)
	{
		const std::string scriptText = call.Node->Params.contains("script") && call.Node->Params["script"].is_string() ? call.Node->Params["script"].get<std::string>() : std::string();
		const std::optional<AssetGuid> guid = AssetGuid::from_string(scriptText);
		if (!guid)
		{
			fail(call, {}, "the script guid is not valid");
			return BtStatus::Failure;
		}
		std::string error;
		ScriptClass* scriptClass = m_Runtime.load(AssetRef{ *guid, 0 }, error);
		if (scriptClass == nullptr)
		{
			fail(call, scriptText, error);
			return BtStatus::Failure;
		}
		const std::string expected = call.Node->Type == "LuaCondition" ? "condition" : "action";
		const std::string kind = bt_leaf_kind(scriptClass->Class);
		if (kind != expected)
		{
			fail(call, scriptClass->Name, kind.empty() ? "the script is not a behaviour tree leaf (extend BtAction or BtCondition)" : "the script is a " + kind + ", the node wants a " + expected);
			return BtStatus::Failure;
		}

		sol::state_view lua(m_Runtime.state().lua_state());
		Leaf leaf;
		leaf.Class = scriptClass->Class;
		leaf.Name = scriptClass->Name;
		leaf.Self = sol::table::create(lua.lua_state());
		sol::table meta = sol::table::create(lua.lua_state());
		meta["__index"] = scriptClass->Class;
		leaf.Self[sol::metatable_key] = meta;

		// the entity of the AI, and the properties: the node's params over the defaults
		if (m_Context.ActiveScene != nullptr && m_Context.ActiveScene->registry().valid(call.Self) && m_Context.ActiveScene->registry().all_of<Id>(call.Self))
		{
			leaf.Self["entity"] = EntityHandle{ m_Context.ActiveScene->registry().get<Id>(call.Self).Guid, &m_Context };
		}
		for (const FieldDescriptor& property : scriptClass->Properties)
		{
			FieldValue value = property.Default;
			const auto param = call.Node->Params.find(property.Name);
			if (param != call.Node->Params.end())
			{
				if (const std::optional<FieldValue> converted = param_to_field(property, *param))
				{
					value = *converted;
				}
				else
				{
					fail(call, scriptClass->Name, "the parameter \"" + property.Name + "\" does not fit the property");
				}
			}
			leaf.Self[property.Name] = field_to_lua(lua, value, property, FieldLink{});
		}

		const sol::object board = blackboard_for(*call.Instance);
		const sol::object onStart = scriptClass->Class["on_start"];
		if (onStart.get_type() == sol::type::function)
		{
			sol::protected_function_result result = m_Runtime.call(onStart.as<sol::protected_function>(), leaf.Self, board);
			if (!result.valid())
			{
				sol::error failure = result;
				fail(call, leaf.Name, std::string("on_start: ") + failure.what());
				return BtStatus::Failure;
			}
		}

		const auto inserted = m_Leaves.emplace(Key{ call.Instance, call.NodeIndex }, std::move(leaf));
		return tick(call, inserted.first->second);
	}

	BtStatus BtLuaHost::tick(BtLeafCall& call, Leaf& leaf)
	{
		const Key key{ call.Instance, call.NodeIndex };
		const sol::object onTick = leaf.Class["on_tick"];
		BtStatus status = BtStatus::Failure;
		if (onTick.get_type() != sol::type::function)
		{
			fail(call, leaf.Name, "the script has no on_tick");
		}
		else
		{
			const sol::object board = blackboard_for(*call.Instance);
			sol::protected_function_result result = m_Runtime.call(onTick.as<sol::protected_function>(), leaf.Self, board, call.Dt);
			if (!result.valid())
			{
				sol::error failureText = result;
				fail(call, leaf.Name, std::string("on_tick: ") + failureText.what());
			}
			else
			{
				const sol::object value = result.get<sol::object>();
				const std::string text = value.get_type() == sol::type::string ? value.as<std::string>() : std::string();
				if (text == "success") { status = BtStatus::Success; }
				else if (text == "failure") { status = BtStatus::Failure; }
				else if (text == "running") { status = BtStatus::Running; }
				else { fail(call, leaf.Name, "on_tick must return \"success\", \"failure\" or \"running\""); }
			}
		}

		if (status != BtStatus::Running)
		{
			m_Leaves.erase(key); // the run is over (leaf is not used after this)
		}
		return status;
	}

	void BtLuaHost::abort(BtLeafCall& call, Leaf& leaf)
	{
		const sol::object onAbort = leaf.Class["on_abort"];
		if (onAbort.get_type() != sol::type::function)
		{
			return;
		}
		const sol::object board = blackboard_for(*call.Instance);
		sol::protected_function_result result = m_Runtime.call(onAbort.as<sol::protected_function>(), leaf.Self, board);
		if (!result.valid())
		{
			sol::error failure = result;
			fail(call, leaf.Name, std::string("on_abort: ") + failure.what());
		}
	}
}
