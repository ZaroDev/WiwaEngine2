#include <pch.h>
#include "script_runtime.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

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

			return Script
		)lua";

		// The globals a script may use. dofile, loadfile, load, io, os, package and debug are not among them.
		constexpr const char* ALLOWED_GLOBALS[] = {
			"assert", "error", "ipairs", "next", "pairs", "pcall", "select", "tonumber", "tostring", "type", "xpcall",
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

		sol::table base = m_Lua.safe_script(SCRIPT_BASE);

		m_Sandbox = m_Lua.create_table();
		for (const char* name : ALLOWED_GLOBALS)
		{
			m_Sandbox[name] = m_Lua[name];
		}
		m_Sandbox["Script"] = base;

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

		m_Sandbox.set_function("log", &Log::log_info);
		m_Sandbox.set_function("log_warning", &Log::log_warning);
		m_Sandbox.set_function("log_error", &Log::log_error);
	}

	ScriptRuntime::~ScriptRuntime() = default;

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
		const auto cached = m_Modules.find(script.Asset.resolved());
		if (cached != m_Modules.end())
		{
			return cached->second.get();
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

		sol::load_result loaded = m_Lua.load(script.Source, "@" + script.Path);
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
		m_Modules[script.Asset.resolved()] = std::move(module);
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
		for (const char* hook : HOOKS)
		{
			const sol::object member = classTable[hook];
			if (member.valid() && member.get_type() != sol::type::lua_nil && member.get_type() != sol::type::function)
			{
				error = script->Path + ": " + hook + " must be a function, not a " + sol::type_name(m_Lua.lua_state(), member.get_type());
				return nullptr;
			}
		}

		std::vector<FieldDescriptor> properties;
		const sol::object propertyObject = classTable["properties"];
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
