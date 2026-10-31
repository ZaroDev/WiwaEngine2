#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

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

		// Calls into Lua always go through here: the call gets a fresh instruction budget, so a script that never returns ends
		// with an error instead of freezing the engine.
		template<typename... Args>
		sol::protected_function_result call(const sol::protected_function& function, Args&&... args)
		{
			begin_call();
			return function(std::forward<Args>(args)...);
		}

		void set_instruction_budget(u64 instructions) { m_Budget = instructions; }
		u64 instruction_budget() const { return m_Budget; }

		// Used by the Lua count hook: true once the call has used more than the budget.
		bool consume_budget(u64 instructions);

	private:
		// A script that has run once: what its chunk returned (a class table, or any module value).
		struct Module
		{
			AssetRef Asset{};
			std::string Path{};
			sol::object Value{};
		};

		void begin_call() { m_Used = 0; }
		Module* run_module(const LoadedScript& script, std::string& error);
		sol::object require_module(std::string path, const std::string& requirer);

		IScriptSource& m_Source;
		sol::state m_Lua{};
		sol::table m_Sandbox{};
		std::map<AssetGuid, std::unique_ptr<ScriptClass>> m_Classes{};
		std::map<AssetGuid, std::unique_ptr<Module>> m_Modules{};
		std::vector<std::string> m_Loading{};                          // the paths being run, innermost last (for cycles)
		std::map<std::string, AssetRef> m_AssetOfPath{};
		std::map<std::string, std::vector<std::string>> m_Requirers{}; // path -> the paths that require it directly
		u64 m_Budget = 50'000'000;
		u64 m_Used = 0;
	};
}
