#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <set>

#include <entt/entt.hpp>
#include <sol/sol.hpp>

#include <scene/reflection/field_descriptor.h>
#include "script_source.h"

namespace wiwa
{
	// A loaded script: the class table it returned and the properties it declared.
	struct ScriptClass
	{
		AssetRef Asset{};
		std::string Path{};
		std::string Name{}; // the file name without extension
		sol::table Class{};
		std::vector<FieldDescriptor> Properties{};
	};

	// The Lua side of scripting: one state (main thread only), a sandbox every script runs in, and the classes loaded from the
	// source. Scripts get their text from an IScriptSource, so the runtime does not care whether they come from the Library, a
	// pak or memory.
	class ScriptRuntime
	{
	public:
		explicit ScriptRuntime(IScriptSource& source);
		~ScriptRuntime();

		ScriptRuntime(const ScriptRuntime&) = delete;
		ScriptRuntime& operator=(const ScriptRuntime&) = delete;

		sol::state& state() { return m_Lua; }
		// The globals every script sees (the allowed libraries, Script, and the engine API the bindings add).
		sol::table& sandbox() { return m_Sandbox; }

		// The class a script asset returned, loading it on first use. Null, with the reason (it names the script) in `error`, if the
		// script cannot be read, does not run, does not return a class table, has a hook that is not a function, or has bad properties.
		ScriptClass* load(AssetRef asset, std::string& error);
		const ScriptClass* find(AssetRef asset) const;

		// Every script that requires this one, directly or through others (for reloading them when it changes).
		std::vector<AssetRef> dependents_of(AssetRef asset) const;

		// Reads the script and every script that requires it again from the source and swaps the new code into the old tables, so
		// that what already holds those tables (instances, other scripts) runs the new code and keeps its state. The components of
		// the classes are registered again; with a registry, the entities that hold them keep the values whose name and type still
		// match. Nothing changes, and `error` says why, if any of the scripts fails to load or has bad properties.
		// `reloaded`, if given, receives every script that was reloaded (the one asked for and the scripts that require it).
		bool reload_script(AssetRef asset, std::string& error, entt::registry* registry = nullptr, std::vector<AssetRef>* reloaded = nullptr);

		// Whether the script has run in this runtime (as a class, or as a module another script required).
		bool knows(AssetRef asset) const { return m_Modules.contains(asset.resolved()) || m_Classes.contains(asset.resolved()); }

		// Calls into Lua always go through here: the call gets a fresh instruction budget, so a script that never returns ends
		// with an error instead of freezing the engine.
		// A call made while another is running (a bound function calling back into Lua) shares the budget of the outer one.
		template<typename... Args>
		sol::protected_function_result call(const sol::protected_function& function, Args&&... args)
		{
			CallScope scope(*this);
			sol::protected_function_result result = function(std::forward<Args>(args)...);
			if (m_Depth > 1 && over_budget())
			{
				// a nested protected call may have swallowed the error that ended the script; pass it on to the outer one
				throw sol::error("script exceeded instruction budget");
			}
			return result;
		}

		// table[key] as a protected call: a metamethod of the table that raises an error gives false and the message, never a panic (sol2
		// aborts the process on an error that no protected call catches). Everything that reads a table a script made goes through here.
		bool protected_get(const sol::table& table, const sol::object& key, sol::object& out, std::string& error);

		void set_instruction_budget(u64 instructions) { m_Budget = instructions; }
		u64 instruction_budget() const { return m_Budget; }

		// Used by the Lua count hook: true once the call has used more than the budget.
		bool consume_budget(u64 instructions);
		// True once the running call went over its budget; the pcall wrappers use it to pass the error on instead of swallowing it.
		bool over_budget() const { return m_Used > m_Budget; }

	private:
		// A script that has run once: what its chunk returned (a class table, or any module value).
		struct Module
		{
			AssetRef Asset{};
			std::string Path{};
			sol::object Value{};
		};

		struct CallScope
		{
			explicit CallScope(ScriptRuntime& runtime) : Runtime(runtime)
			{
				if (Runtime.m_Depth++ == 0)
				{
					Runtime.m_Used = 0;
				}
			}
			~CallScope() { Runtime.m_Depth--; }
			ScriptRuntime& Runtime;
		};
		Module* run_module(const LoadedScript& script, std::string& error);
		sol::object require_module(std::string path, const std::string& requirer);

		IScriptSource& m_Source;
		sol::state m_Lua{};
		sol::table m_Sandbox{};
		sol::protected_function m_Get{}; // (declared after m_Lua so that it goes first) function(table, key) return table[key] end
		std::map<AssetGuid, std::unique_ptr<ScriptClass>> m_Classes{};
		std::map<AssetGuid, std::unique_ptr<Module>> m_Modules{};
		bool m_Reloading = false;                                  // reload_script is running the new versions
		std::set<AssetGuid> m_Stale{};                             // the scripts it runs again
		std::map<AssetGuid, std::unique_ptr<Module>> m_Fresh{};    // their new versions, until they replace the old ones
		std::vector<std::string> m_Loading{};                          // the paths being run, innermost last (for cycles)
		std::map<std::string, AssetRef> m_AssetOfPath{};
		std::map<std::string, std::vector<std::string>> m_Requirers{}; // path -> the paths that require it directly
		u64 m_Budget = 50'000'000;
		u64 m_Used = 0;
		u32 m_Depth = 0;
	};
}
