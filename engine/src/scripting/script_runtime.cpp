#include <pch.h>
#include "script_runtime.h"

#include <ai/bt_lua_host.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "bindings/bindings.h"
#include "script_component.h"
#include "script_properties.h"
#include "script_requires.h"

#define WI_SCRIPT_LOG(level, ...) WI_LOG(level, "Scripting", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		constexpr int HOOK_STEP = 1000; // VM instructions between two calls of the count hook
		constexpr const char* RUNTIME_KEY = "wiwa.script_runtime";

		void budget_hook(lua_State* state, lua_Debug*)
		{
			lua_getfield(state, LUA_REGISTRYINDEX, RUNTIME_KEY);
			ScriptRuntime* runtime = static_cast<ScriptRuntime*>(lua_touserdata(state, -1));
			lua_pop(state, 1);
			if (runtime != nullptr && runtime->consume_budget(HOOK_STEP))
			{
				luaL_error(state, "script exceeded instruction budget");
			}
		}

		// The base class every script extends. A class table inherits from its parent through its metatable and merges the parent's
		// properties with its own.
		constexpr const char* SCRIPT_BASE = R"lua(
			local raw_pcall, raw_xpcall, over_budget = pcall, xpcall, __wiwa_over_budget

			-- pcall that does not swallow the end of a script that ran out of budget: without it, `while true do pcall(f) end` never ends
			local function safe_pcall(f, ...)
				local r = table.pack(raw_pcall(f, ...))
				if over_budget() then error("script exceeded instruction budget", 0) end
				return table.unpack(r, 1, r.n)
			end

			local function safe_xpcall(f, handler, ...)
				local r = table.pack(raw_xpcall(f, handler, ...))
				if over_budget() then error("script exceeded instruction budget", 0) end
				return table.unpack(r, 1, r.n)
			end

			local Script = {}
			Script.__index = Script
			Script.properties = {}

			function Script:extend(definition)
				local class = definition or {}
				local properties = {}
				for name, property in pairs(self.properties or {}) do properties[name] = property end
				for name, property in pairs(class.properties or {}) do properties[name] = property end
				class.properties = properties
				class.__index = class
				return setmetatable(class, self)
			end

			local function get(table_, key) return table_[key] end

			return Script, safe_pcall, safe_xpcall, get
		)lua";

		// The globals a script may use. dofile, loadfile, load, io, os, package and debug are not among them.
		constexpr const char* ALLOWED_GLOBALS[] = {
			"assert", "error", "ipairs", "next", "pairs", "select", "tonumber", "tostring", "type",
			"rawequal", "rawget", "rawlen", "rawset", "setmetatable", "getmetatable",
			"math", "string", "table", "utf8",
		};

		constexpr const char* HOOKS[] = { "on_init", "on_update", "on_destroy" };
	}

	ScriptRuntime::ScriptRuntime(IScriptSource& source) : m_Source(source)
	{
		m_Lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::utf8);

		// a C++ exception inside a bound function becomes a Lua error with its message (sol2 would also print it)
		m_Lua.set_exception_handler([](lua_State* state, sol::optional<const std::exception&> exception, sol::string_view description)
			{
				return sol::stack::push(state, exception ? std::string(exception->what()) : std::string(description));
			});

		lua_pushlightuserdata(m_Lua.lua_state(), this);
		lua_setfield(m_Lua.lua_state(), LUA_REGISTRYINDEX, RUNTIME_KEY);
		lua_sethook(m_Lua.lua_state(), budget_hook, LUA_MASKCOUNT, HOOK_STEP);

		m_Lua.set_function("__wiwa_over_budget", [this]() { return over_budget(); });
		sol::protected_function_result prelude = m_Lua.safe_script(SCRIPT_BASE);
		sol::table base = prelude.get<sol::table>(0);

		m_Sandbox = m_Lua.create_table();
		for (const char* name : ALLOWED_GLOBALS)
		{
			m_Sandbox[name] = m_Lua[name];
		}
		m_Sandbox["Script"] = base;
		m_Sandbox["pcall"] = prelude.get<sol::object>(1);
		m_Sandbox["xpcall"] = prelude.get<sol::object>(2);
		m_Get = prelude.get<sol::protected_function>(3);
		define_bt_lua_bases(*this); // BtAction / BtCondition: every runtime knows them, the script importer's too

		m_Sandbox.set_function("require", [this](sol::this_state state, const std::string& path) -> sol::object
			{
				// who is asking: the chunk name of the Lua function that called require ("@assets/scripts/x.lua")
				std::string requirer;
				lua_Debug caller;
				if (lua_getstack(state.L, 1, &caller) && lua_getinfo(state.L, "S", &caller) && caller.source != nullptr && caller.source[0] == '@')
				{
					requirer = caller.source + 1;
				}
				return require_module(path, requirer);
			});

		// the math types are part of the language scripts are written in: a property default may be vec3(0, 1, 0) when the script is only
		// being imported, with no scene around
		bind_math(*this);

		m_Sandbox.set_function("log", &Log::log_info);
		m_Sandbox.set_function("log_warning", &Log::log_warning);
		m_Sandbox.set_function("log_error", &Log::log_error);
	}

	ScriptRuntime::~ScriptRuntime() = default;

	bool ScriptRuntime::protected_get(const sol::table& table, const sol::object& key, sol::object& out, std::string& error)
	{
		sol::protected_function_result result = call(m_Get, table, key);
		if (!result.valid())
		{
			sol::error failure = result;
			error = failure.what();
			return false;
		}
		out = result.get<sol::object>();
		return true;
	}

	bool ScriptRuntime::consume_budget(u64 instructions)
	{
		m_Used += instructions;
		return m_Used > m_Budget;
	}

	const ScriptClass* ScriptRuntime::find(AssetRef asset) const
	{
		const auto found = m_Classes.find(asset.resolved());
		return found == m_Classes.end() ? nullptr : found->second.get();
	}

	ScriptRuntime::Module* ScriptRuntime::run_module(const LoadedScript& script, std::string& error)
	{
		const AssetGuid key = script.Asset.resolved();
		const bool stale = m_Reloading && m_Stale.contains(key);
		if (stale)
		{
			const auto fresh = m_Fresh.find(key);
			if (fresh != m_Fresh.end())
			{
				return fresh->second.get();
			}
		}
		else
		{
			const auto cached = m_Modules.find(key);
			if (cached != m_Modules.end())
			{
				return cached->second.get();
			}
		}

		const auto running = std::find(m_Loading.begin(), m_Loading.end(), script.Path);
		if (running != m_Loading.end())
		{
			error = "require cycle: ";
			for (auto it = running; it != m_Loading.end(); ++it)
			{
				error += *it + " -> ";
			}
			error += script.Path;
			return nullptr;
		}

		m_AssetOfPath[script.Path] = script.Asset;
		m_Loading.push_back(script.Path);
		struct Pop
		{
			std::vector<std::string>& Stack;
			~Pop() { Stack.pop_back(); }
		} pop{ m_Loading };

		// text only: precompiled chunks are not checked by Lua, and a pack or file could hold a hostile one
		sol::load_result loaded = m_Lua.load(script.Source, "@" + script.Path, sol::load_mode::text);
		if (!loaded.valid())
		{
			sol::error failure = loaded;
			error = failure.what();
			return nullptr;
		}

		sol::protected_function chunk = loaded;
		sol::environment environment(m_Lua, sol::create, m_Sandbox);
		sol::set_environment(environment, chunk);

		sol::protected_function_result result = call(chunk);
		if (!result.valid())
		{
			sol::error failure = result;
			error = failure.what();
			return nullptr;
		}

		auto module = std::make_unique<Module>();
		module->Asset = script.Asset;
		module->Path = script.Path;
		module->Value = sol::object(result);

		Module* raw = module.get();
		(stale ? m_Fresh : m_Modules)[key] = std::move(module);
		return raw;
	}

	sol::object ScriptRuntime::require_module(std::string path, const std::string& requirer)
	{
		path = normalize_require_path(std::move(path));

		const std::optional<LoadedScript> script = m_Source.find_by_path(path);
		if (!script)
		{
			throw sol::error("require: there is no script at '" + path + "'");
		}

		if (!requirer.empty())
		{
			std::vector<std::string>& requirers = m_Requirers[path];
			if (std::find(requirers.begin(), requirers.end(), requirer) == requirers.end())
			{
				requirers.push_back(requirer);
			}
		}

		std::string error;
		Module* module = run_module(*script, error);
		if (module == nullptr)
		{
			throw sol::error(error);
		}
		return module->Value;
	}

	std::vector<AssetRef> ScriptRuntime::dependents_of(AssetRef asset) const
	{
		std::string start;
		for (const auto& [path, ref] : m_AssetOfPath)
		{
			if (ref == asset)
			{
				start = path;
			}
		}

		std::vector<std::string> pending{ start };
		std::vector<std::string> seen{ start };
		std::vector<AssetRef> dependents;
		while (!pending.empty())
		{
			const std::string current = pending.back();
			pending.pop_back();

			const auto requirers = m_Requirers.find(current);
			if (requirers == m_Requirers.end())
			{
				continue;
			}
			for (const std::string& requirer : requirers->second)
			{
				if (std::find(seen.begin(), seen.end(), requirer) != seen.end())
				{
					continue;
				}
				seen.push_back(requirer);
				pending.push_back(requirer);
				const auto ref = m_AssetOfPath.find(requirer);
				if (ref != m_AssetOfPath.end())
				{
					dependents.push_back(ref->second);
				}
			}
		}
		return dependents;
	}

	namespace
	{
		// Makes `target` hold what `source` holds, keeping target's identity: keys that source lacks go, the others are copied, and a
		// reference to source itself becomes one to target. `renamed` maps the new tables of modules that were swapped before this one
		// to their old ones (a class's parent, in its metatable).
		void swap_table_in_place(sol::table target, sol::table source, const std::vector<std::pair<sol::table, sol::table>>& renamed)
		{
			const auto translate = [&](const sol::object& value) -> sol::object
			{
				if (value == source)
				{
					return target;
				}
				for (const auto& [fresh, old] : renamed)
				{
					if (value == fresh)
					{
						return old;
					}
				}
				return value;
			};

			std::vector<sol::object> removed;
			for (const auto& [key, value] : target)
			{
				if (!source.raw_get<sol::object>(key).valid() || source.raw_get<sol::object>(key).get_type() == sol::type::lua_nil)
				{
					removed.push_back(key);
				}
			}
			for (const sol::object& key : removed)
			{
				target.raw_set(key, sol::lua_nil);
			}

			for (const auto& [key, value] : source)
			{
				target.raw_set(key, translate(value));
			}

			const sol::object meta = source[sol::metatable_key];
			if (meta.valid() && meta.get_type() == sol::type::table)
			{
				target[sol::metatable_key] = translate(meta);
			}
		}
	}

	bool ScriptRuntime::reload_script(AssetRef asset, std::string& error, entt::registry* registry, std::vector<AssetRef>* reloaded)
	{
		error.clear();

		std::vector<AssetRef> affected{ asset };
		for (const AssetRef& dependent : dependents_of(asset))
		{
			affected.push_back(dependent);
		}

		m_Reloading = true;
		m_Stale.clear();
		m_Fresh.clear();
		for (const AssetRef& ref : affected)
		{
			m_Stale.insert(ref.resolved());
		}

		const auto abandon = [&]()
		{
			m_Reloading = false;
			m_Stale.clear();
			m_Fresh.clear();
			return false;
		};

		// 1. run the new versions, changing nothing yet
		std::map<AssetGuid, std::vector<FieldDescriptor>> properties;
		for (const AssetRef& ref : affected)
		{
			const std::optional<LoadedScript> script = m_Source.find_by_asset(ref);
			if (!script)
			{
				error = "script " + ref.Guid.to_string() + " is not available";
				return abandon();
			}

			Module* module = run_module(*script, error);
			if (module == nullptr)
			{
				return abandon();
			}

			if (!m_Classes.contains(ref.resolved()))
			{
				continue; // a module that is not a script class: its table is swapped below
			}

			const sol::object& value = module->Value;
			if (!value.is<sol::table>())
			{
				error = script->Path + ": a script must return a class table (Script:extend{...}), not a " + sol::type_name(m_Lua.lua_state(), value.get_type());
				return abandon();
			}

			sol::table classTable = value.as<sol::table>();
			std::string propertyError;
			std::vector<FieldDescriptor> parsed;
			try
			{
				std::string lookupError;
				for (const char* hook : HOOKS)
				{
					sol::object member;
					if (!protected_get(classTable, sol::make_object(m_Lua.lua_state(), hook), member, lookupError))
					{
						error = script->Path + ": " + hook + ": " + lookupError;
						return abandon();
					}
					if (member.valid() && member.get_type() != sol::type::lua_nil && member.get_type() != sol::type::function)
					{
						error = script->Path + ": " + hook + " must be a function";
						return abandon();
					}
				}

				sol::object propertyObject;
				if (!protected_get(classTable, sol::make_object(m_Lua.lua_state(), "properties"), propertyObject, lookupError))
				{
					error = script->Path + ": properties: " + lookupError;
					return abandon();
				}
				if (propertyObject.is<sol::table>())
				{
					parsed = parse_properties(propertyObject.as<sol::table>(), propertyError);
				}
			}
			catch (const std::exception& exception)
			{
				propertyError = exception.what();
			}
			if (!propertyError.empty())
			{
				error = script->Path + ": " + propertyError;
				return abandon();
			}
			properties[ref.resolved()] = std::move(parsed);
		}

		// 2. everything loaded: swap the new code into the old tables, the changed script first (the ones that require it follow)
		std::vector<std::pair<sol::table, sol::table>> renamed;
		for (const AssetRef& ref : affected)
		{
			const AssetGuid key = ref.resolved();
			const auto fresh = m_Fresh.find(key);
			if (fresh == m_Fresh.end())
			{
				continue;
			}

			const auto old = m_Modules.find(key);
			if (old != m_Modules.end() && old->second->Value.is<sol::table>() && fresh->second->Value.is<sol::table>())
			{
				sol::table oldTable = old->second->Value.as<sol::table>();
				sol::table freshTable = fresh->second->Value.as<sol::table>();
				swap_table_in_place(oldTable, freshTable, renamed);
				renamed.emplace_back(freshTable, oldTable);
			}
			else
			{
				m_Modules[key] = std::move(fresh->second);
			}
		}

		for (const auto& [key, fields] : properties)
		{
			ScriptClass& scriptClass = *m_Classes[key];
			scriptClass.Properties = fields;
			register_script_component(scriptClass, registry);
		}

		m_Reloading = false;
		m_Stale.clear();
		m_Fresh.clear();
		if (reloaded != nullptr)
		{
			*reloaded = affected;
		}
		return true;
	}

	ScriptClass* ScriptRuntime::load(AssetRef asset, std::string& error)
	{
		error.clear();

		const auto cached = m_Classes.find(asset.resolved());
		if (cached != m_Classes.end())
		{
			return cached->second.get();
		}

		const std::optional<LoadedScript> script = m_Source.find_by_asset(asset);
		if (!script)
		{
			error = "script " + asset.Guid.to_string() + " is not available";
			return nullptr;
		}

		Module* module = run_module(*script, error);
		if (module == nullptr)
		{
			return nullptr;
		}

		const sol::object& returned = module->Value;
		if (!returned.is<sol::table>())
		{
			error = script->Path + ": a script must return a class table (Script:extend{...}), not a " + sol::type_name(m_Lua.lua_state(), returned.get_type());
			return nullptr;
		}

		sol::table classTable = returned.as<sol::table>();
		std::vector<FieldDescriptor> properties;
		try
		{
			std::string lookupError;
			for (const char* hook : HOOKS)
			{
				sol::object member;
				if (!protected_get(classTable, sol::make_object(m_Lua.lua_state(), hook), member, lookupError))
				{
					error = script->Path + ": " + hook + ": " + lookupError;
					return nullptr;
				}
				if (member.valid() && member.get_type() != sol::type::lua_nil && member.get_type() != sol::type::function)
				{
					error = script->Path + ": " + hook + " must be a function, not a " + sol::type_name(m_Lua.lua_state(), member.get_type());
					return nullptr;
				}
			}

			sol::object propertyObject;
			if (!protected_get(classTable, sol::make_object(m_Lua.lua_state(), "properties"), propertyObject, lookupError))
			{
				error = script->Path + ": properties: " + lookupError;
				return nullptr;
			}
			if (propertyObject.is<sol::table>())
			{
				std::string propertyError;
				properties = parse_properties(propertyObject.as<sol::table>(), propertyError);
				if (!propertyError.empty())
				{
					error = script->Path + ": " + propertyError;
					return nullptr;
				}
			}
		}
		catch (const std::exception& exception)
		{
			// the class has a metamethod that failed while it was read
			error = script->Path + ": " + exception.what();
			return nullptr;
		}

		auto scriptClass = std::make_unique<ScriptClass>();
		scriptClass->Asset = asset;
		scriptClass->Path = script->Path;
		scriptClass->Name = std::filesystem::path(script->Path).stem().string();
		scriptClass->Class = std::move(classTable);
		scriptClass->Properties = std::move(properties);

		ScriptClass* raw = scriptClass.get();
		m_Classes[asset.resolved()] = std::move(scriptClass);
		return raw;
	}
}
