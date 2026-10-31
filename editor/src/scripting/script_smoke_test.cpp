#include <editor_common.h>
#include "script_smoke_test.h"

#include <algorithm>

#include <assets/wasset_io.h>
#include <scene/reflection/component_registry.h>
#include <scripting/script_component.h>
#include <scripting/script_requires.h>
#include <scripting/script_runtime.h>

#include "asset_pipeline/asset_database.h"
#include "asset_pipeline/builtin_importers.h"
#include "asset_pipeline/file_utils.h"

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

		void write_text(const std::filesystem::path& path, const std::string& text)
		{
			std::error_code error;
			std::filesystem::create_directories(path.parent_path(), error);
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			out << text;
		}

		// The cooked file of an asset, read back from the Library.
		struct Cooked
		{
			bool Found = false;
			WAssetHeader Header{};
			std::optional<ScriptData> Data{};
		};

		Cooked read_cooked(const AssetDatabase& database, AssetRef asset)
		{
			Cooked cooked;
			std::error_code error;
			for (auto it = std::filesystem::recursive_directory_iterator(database.get_library_root(), error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
			{
				if (!it->is_regular_file() || it->path().extension() != ".wasset")
				{
					continue;
				}
				std::optional<std::vector<std::byte>> bytes = read_binary_file(it->path());
				std::span<const std::byte> payload;
				std::optional<WAssetHeader> header = bytes ? parse_wasset_header(*bytes, payload) : std::nullopt;
				if (header && header->Guid == asset.Guid)
				{
					cooked.Found = true;
					cooked.Header = *header;
					cooked.Data = deserialize<ScriptData>(payload);
				}
			}
			return cooked;
		}

		void check_require_scan(Check& check)
		{
			const RequireScan scan = scan_requires(R"(
				local a = require("assets/scripts/a")
				local b = require 'assets/scripts/b.lua'
				local c = require("assets/scripts/a.lua")
				-- local d = require("assets/scripts/commented")
				--[[ require("assets/scripts/block") ]]
				local e = require(name)
			)");
			check(scan.Paths.size() == 2 && scan.Paths[0] == "assets/scripts/a.lua" && scan.Paths[1] == "assets/scripts/b.lua", "require_scan_finds_literals_once_and_skips_comments");
			check(scan.DynamicCalls == 1, "require_scan_counts_dynamic_calls");
		}

		void check_importer(Check& check)
		{
			std::error_code error;
			const std::filesystem::path temp = std::filesystem::temp_directory_path() / "wiwa_script_smoke";
			std::filesystem::remove_all(temp, error);

			const std::filesystem::path scripts = temp / "assets" / "scripts";
			const std::string root = (temp / "assets").generic_string();
			write_text(scripts / "util.lua", "return { answer = 42 }");
			write_text(scripts / "player.lua", "local util = require('" + root + "/scripts/util.lua')\nreturn Script:extend{ properties = { speed = { type = 'float', default = 5 }, label = { type = 'string' } } }");
			write_text(scripts / "broken.lua", "local x = \nreturn Script:extend{");
			write_text(scripts / "cycle_a.lua", "return require('" + root + "/scripts/cycle_b.lua')");
			write_text(scripts / "cycle_b.lua", "return require('" + root + "/scripts/cycle_a.lua')");
			write_text(scripts / "self_cycle.lua", "return require('" + root + "/scripts/self_cycle.lua')");
			write_text(scripts / "dynamic.lua", "local base = '" + root + "/scripts/'\nlocal m = require(base .. 'util')\nreturn Script:extend{}");

			AssetDatabase database(temp / "assets", temp / "library");
			register_builtin_importers(database);
			database.scan();

			const std::optional<AssetRef> util = database.ensure_imported(scripts / "util.lua");
			const std::optional<AssetRef> player = database.ensure_imported(scripts / "player.lua");
			check(util.has_value() && player.has_value(), "importer_accepts_valid_script");

			if (player)
			{
				const Cooked cooked = read_cooked(database, *player);
				check(cooked.Found && cooked.Data && cooked.Data->Source.find("Script:extend") != std::string::npos, "importer_keeps_the_source");
				check(cooked.Data && cooked.Data->SourcePath == root + "/scripts/player.lua", "importer_records_the_catalog_path");
				check(cooked.Found && util && cooked.Header.Dependencies.size() == 1 && cooked.Header.Dependencies[0] == util->resolved(), "importer_records_require_dependency");

				const std::optional<ComponentSchema> schema = ComponentRegistry::get().schema(script_component_name(player->Guid));
				check(schema && schema->Fields.size() == 2 && schema->Fields[0].Name == "label" && schema->Fields[1].Name == "speed", "property_schema_registered_at_import");
				check(schema && schema->find_field("speed") != nullptr && schema->find_field("speed")->Type == FieldType::Float, "property_schema_has_the_declared_types");

				// a property that changes type: its value goes back to the default, the others are kept
				const ComponentInfo* info = ComponentRegistry::get().find(script_component_name(player->Guid));
				check(info != nullptr && info->DisplayName == "player", "script_component_is_shown_by_file_name");
				if (info != nullptr)
				{
					entt::registry registry;
					const entt::entity entity = registry.create();
					info->Add(registry, entity);
					info->Set(registry, entity, static_cast<size_t>(info->field_index("speed")), 3.0f);
					info->Set(registry, entity, static_cast<size_t>(info->field_index("label")), std::string("hero"));

					ScriptClass changed;
					changed.Asset = *player;
					changed.Name = "player";
					for (const char* name : { "label", "speed" })
					{
						FieldDescriptor field;
						field.Name = name;
						field.Type = FieldType::String;
						field.Default = std::string("none");
						changed.Properties.push_back(field);
					}
					register_script_component(changed, &registry);

					const ComponentInfo* after = ComponentRegistry::get().find(script_component_name(player->Guid));
					const FieldValue speed = after != nullptr ? after->Get(registry, entity, static_cast<size_t>(after->field_index("speed"))) : FieldValue{};
					const FieldValue label = after != nullptr ? after->Get(registry, entity, static_cast<size_t>(after->field_index("label"))) : FieldValue{};
					check(std::holds_alternative<std::string>(speed) && std::get<std::string>(speed) == "none", "retyped_property_reconciles: retyped value takes the default");
					check(std::holds_alternative<std::string>(label) && std::get<std::string>(label) == "hero", "retyped_property_reconciles: other values are kept");
				}
			}

			// a failed import still has a guid (its .meta), but nothing was cooked for it
			const auto rejected = [&](const char* file)
			{
				const std::optional<AssetRef> asset = database.ensure_imported(scripts / file);
				return asset.has_value() && !read_cooked(database, *asset).Found;
			};
			const auto accepted = [&](const char* file)
			{
				const std::optional<AssetRef> asset = database.ensure_imported(scripts / file);
				return asset.has_value() && read_cooked(database, *asset).Found;
			};
			check(rejected("broken.lua"), "importer_rejects_syntax_error");
			check(rejected("cycle_a.lua") && rejected("cycle_b.lua"), "importer_rejects_require_cycle");
			check(rejected("self_cycle.lua"), "importer_rejects_self_require");
			check(accepted("dynamic.lua"), "importer_accepts_dynamic_require_with_a_warning");

			std::filesystem::remove_all(temp, error);
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
		check_require_scan(check);
		check_importer(check);

		if (check.Failures == 0)
		{
			WI_SCRIPT_LOG(Info, "Script smoke test passed");
		}
		return check.Failures == 0;
	}
}
