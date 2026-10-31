#include <editor_common.h>
#include "script_smoke_test.h"

#include <algorithm>

#include <assets/wasset_io.h>
#include <scripting/script_runtime.h>

#define WI_SCRIPT_LOG(level, ...) WI_LOG(level, "Scripting", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		struct Check
		{
			u32 Failures = 0;

			void operator()(bool condition, const std::string& what)
			{
				if (!condition)
				{
					Failures++;
					WI_SCRIPT_LOG(Error, "Smoke test FAILED: {0}", what);
				}
			}
		};

		void check_script_data(Check& check)
		{
			ScriptData data;
			data.Source = "return 1";
			data.SourcePath = "assets/scripts/a.lua";

			const std::vector<std::byte> bytes = serialize(data);
			const std::optional<ScriptData> back = deserialize<ScriptData>(bytes);
			check(back.has_value() && back->Source == data.Source && back->SourcePath == data.SourcePath, "script_data_roundtrips");

			bool allRejected = true;
			for (size_t size = 0; size < bytes.size(); size++)
			{
				allRejected = allRejected && !deserialize<ScriptData>(std::span<const std::byte>(bytes.data(), size)).has_value();
			}
			check(allRejected, "script_data_rejects_truncated");
		}

		// Scripts from memory, by path.
		class MemorySource final : public IScriptSource
		{
		public:
			AssetRef add(const std::string& path, const std::string& source)
			{
				LoadedScript script;
				script.Asset = AssetRef{ AssetGuid::generate(), 0 };
				script.Path = path;
				script.Source = source;
				m_Scripts.push_back(script);
				return script.Asset;
			}

			virtual std::optional<LoadedScript> find_by_path(std::string_view path) override
			{
				for (const LoadedScript& script : m_Scripts)
				{
					if (script.Path == path)
					{
						return script;
					}
				}
				return std::nullopt;
			}

			virtual std::optional<LoadedScript> find_by_asset(AssetRef ref) override
			{
				for (const LoadedScript& script : m_Scripts)
				{
					if (script.Asset == ref)
					{
						return script;
					}
				}
				return std::nullopt;
			}

		private:
			std::vector<LoadedScript> m_Scripts{};
		};

		bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

		void check_runtime_classes(Check& check)
		{
			{
				MemorySource source;
				const AssetRef ref = source.add("assets/scripts/player.lua", R"(
					return Script:extend{ properties = { speed = { type = "float", default = 5, min = 0, max = 20 } } }
				)");
				ScriptRuntime runtime(source);
				std::string error;
				const ScriptClass* cls = runtime.load(ref, error);
				check(cls != nullptr && error.empty(), "class_loads_with_properties: loads");
				if (cls != nullptr)
				{
					check(cls->Name == "player", "class_loads_with_properties: name is the file stem");
					check(cls->Properties.size() == 1, "class_loads_with_properties: one property");
					if (cls->Properties.size() == 1)
					{
						const FieldDescriptor& field = cls->Properties[0];
						check(field.Name == "speed" && field.Type == FieldType::Float, "class_loads_with_properties: name and type");
						check(std::holds_alternative<float>(field.Default) && std::get<float>(field.Default) == 5.0f, "class_loads_with_properties: default");
						check(field.Meta.HasRange && field.Meta.Min == 0.0f && field.Meta.Max == 20.0f, "class_loads_with_properties: range");
					}
				}
			}

			{
				MemorySource source;
				const AssetRef ref = source.add("assets/scripts/child.lua", R"(
					local Base = Script:extend{ properties = { speed = { type = "float", default = 1 } } }
					return Base:extend{ properties = { lives = { type = "int", default = 3 } } }
				)");
				ScriptRuntime runtime(source);
				std::string error;
				const ScriptClass* cls = runtime.load(ref, error);
				check(cls != nullptr && cls->Properties.size() == 2, "subclass_inherits_properties: both properties");
				if (cls != nullptr && cls->Properties.size() == 2)
				{
					check(cls->Properties[0].Name == "lives" && cls->Properties[1].Name == "speed", "subclass_inherits_properties: sorted by name");
				}
			}

			{
				MemorySource source;
				const AssetRef ref = source.add("assets/scripts/bad_return.lua", "return 1");
				ScriptRuntime runtime(source);
				std::string error;
				check(runtime.load(ref, error) == nullptr && contains(error, "assets/scripts/bad_return.lua"), "non_table_return_is_error");
			}

			{
				MemorySource source;
				const AssetRef ref = source.add("assets/scripts/bad_hook.lua", "return Script:extend{ on_update = 5 }");
				ScriptRuntime runtime(source);
				std::string error;
				check(runtime.load(ref, error) == nullptr && contains(error, "on_update") && contains(error, "assets/scripts/bad_hook.lua"), "non_function_hook_is_error");
			}

			{
				MemorySource source;
				const AssetRef ref = source.add("assets/scripts/no_hooks.lua", "return Script:extend{}");
				ScriptRuntime runtime(source);
				std::string error;
				check(runtime.load(ref, error) != nullptr, "script_without_hooks_loads");
			}

			{
				MemorySource source;
				const AssetRef ref = source.add("assets/scripts/bad_prop.lua", R"(return Script:extend{ properties = { x = { type = "banana" } } })");
				ScriptRuntime runtime(source);
				std::string error;
				check(runtime.load(ref, error) == nullptr && contains(error, "banana") && contains(error, "x"), "bad_property_type_is_error");
			}

			{
				MemorySource source;
				const AssetRef ref = source.add("assets/scripts/syntax.lua", "return Script:extend{");
				ScriptRuntime runtime(source);
				std::string error;
				check(runtime.load(ref, error) == nullptr && contains(error, "assets/scripts/syntax.lua"), "syntax_error_names_the_script");
			}
		}

		void check_sandbox(Check& check)
		{
			MemorySource source;
			const AssetRef ref = source.add("assets/scripts/sandbox.lua", R"(
				return Script:extend{ probe = function() return io == nil and os == nil and dofile == nil and loadfile == nil and load == nil and package == nil and debug == nil end }
			)");
			ScriptRuntime runtime(source);
			std::string error;
			const ScriptClass* cls = runtime.load(ref, error);
			check(cls != nullptr, "sandbox_has_no_io_os: loads");
			if (cls != nullptr)
			{
				sol::protected_function_result result = runtime.call(sol::protected_function(cls->Class["probe"]));
				check(result.valid() && result.get<bool>(), "sandbox_has_no_io_os");
			}
		}

		void check_budget(Check& check)
		{
			MemorySource source;
			const AssetRef ref = source.add("assets/scripts/loop.lua", R"(
				return Script:extend{ on_update = function() while true do end end }
			)");
			ScriptRuntime runtime(source);
			std::string error;
			const ScriptClass* cls = runtime.load(ref, error);
			check(cls != nullptr, "infinite_loop_is_stopped: loads");
			if (cls != nullptr)
			{
				runtime.set_instruction_budget(100000);
				sol::protected_function_result result = runtime.call(sol::protected_function(cls->Class["on_update"]));
				bool stopped = false;
				if (!result.valid())
				{
					sol::error failure = result;
					stopped = contains(failure.what(), "instruction budget");
				}
				check(stopped, "infinite_loop_is_stopped");
			}
		}

		void check_require(Check& check)
		{
			{
				MemorySource source;
				source.add("assets/scripts/util.lua", "return { answer = 42 }");
				const AssetRef ref = source.add("assets/scripts/user.lua", R"(
					local util = require("assets/scripts/util.lua")
					return Script:extend{ value = util.answer }
				)");
				ScriptRuntime runtime(source);
				std::string error;
				const ScriptClass* cls = runtime.load(ref, error);
				check(cls != nullptr && cls->Class["value"].get<int>() == 42, "require_returns_module");
			}

			{
				MemorySource source;
				source.add("assets/scripts/counted.lua", "tick() return {}");
				const AssetRef ref = source.add("assets/scripts/twice.lua", R"(
					require("assets/scripts/counted.lua")
					require("assets/scripts/counted")
					return Script:extend{}
				)");
				ScriptRuntime runtime(source);
				int ticks = 0;
				runtime.sandbox().set_function("tick", [&ticks]() { ticks++; });
				std::string error;
				check(runtime.load(ref, error) != nullptr && ticks == 1, "require_is_cached (with or without extension)");
			}

			{
				MemorySource source;
				source.add("assets/scripts/cycle_b.lua", R"(return require("assets/scripts/cycle_a.lua"))");
				const AssetRef ref = source.add("assets/scripts/cycle_a.lua", R"(return require("assets/scripts/cycle_b.lua"))");
				ScriptRuntime runtime(source);
				std::string error;
				check(runtime.load(ref, error) == nullptr && contains(error, "cycle_a.lua") && contains(error, "cycle_b.lua") && contains(error, "->"), "require_cycle_errors");
			}

			{
				MemorySource source;
				const AssetRef ref = source.add("assets/scripts/needs_missing.lua", R"(require("assets/scripts/nope.lua") return Script:extend{})");
				ScriptRuntime runtime(source);
				std::string error;
				check(runtime.load(ref, error) == nullptr && contains(error, "assets/scripts/nope.lua"), "require_missing_errors");
			}

			{
				MemorySource source;
				const AssetRef c = source.add("assets/scripts/c.lua", "return {}");
				const AssetRef b = source.add("assets/scripts/b.lua", R"(require("assets/scripts/c.lua") return {})");
				const AssetRef a = source.add("assets/scripts/a.lua", R"(require("assets/scripts/b.lua") return Script:extend{})");
				ScriptRuntime runtime(source);
				std::string error;
				check(runtime.load(a, error) != nullptr, "dependents_are_transitive: loads");
				const std::vector<AssetRef> dependents = runtime.dependents_of(c);
				const auto has = [&](AssetRef ref) { return std::find(dependents.begin(), dependents.end(), ref) != dependents.end(); };
				check(dependents.size() == 2 && has(a) && has(b), "dependents_are_transitive");
				check(runtime.dependents_of(a).empty(), "nothing_depends_on_the_top_script");
			}

			{
				MemorySource source;
				const AssetRef base = source.add("assets/scripts/base.lua", "return Script:extend{ properties = { hp = { type = \"int\", default = 10 } } }");
				const AssetRef child = source.add("assets/scripts/child2.lua", R"(
					local Base = require("assets/scripts/base.lua")
					return Base:extend{}
				)");
				ScriptRuntime runtime(source);
				std::string error;
				const ScriptClass* childClass = runtime.load(child, error);
				const ScriptClass* baseClass = runtime.load(base, error);
				check(childClass != nullptr && baseClass != nullptr && childClass->Properties.size() == 1, "require_shares_class_with_load: inherits properties across files");
				check(baseClass != nullptr && runtime.dependents_of(base).size() == 1, "require_shares_class_with_load: one dependent");
			}
		}
	}

	bool run_script_smoke_test(AssetPipelineModule& pipeline)
	{
		(void)pipeline;
		Check check;

		check_script_data(check);
		check_runtime_classes(check);
		check_sandbox(check);
		check_budget(check);
		check_require(check);

		if (check.Failures == 0)
		{
			WI_SCRIPT_LOG(Info, "Script smoke test passed");
		}
		return check.Failures == 0;
	}
}
