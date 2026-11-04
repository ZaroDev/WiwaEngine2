#include <editor_common.h>
#include "script_smoke_test.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iterator>
#include <set>
#include <thread>
#include <unordered_map>

#include <assets/wasset_io.h>
#include <scene/prefab_expander.h>
#include <scene/scene_clone.h>
#include <scene/reflection/component_registry.h>
#include <scene/reflection/value_bag.h>
#include <scene/scene.h>
#include <scene/scene_records.h>
#include <scripting/asset_script_source.h>
#include <scripting/script_component.h>
#include <assets/asset_manager.h>
#include <assets/pak_source.h>
#include <core/engine.h>
#include <renderer/debug/debug_draw.h>
#include <scene/scene_module.h>
#include <scripting/script_context.h>
#include <scripting/bindings/script_types.h>
#include <scripting/script_instances.h>
#include <scripting/script_reload.h>
#include <scripting/script_requires.h>
#include <scripting/script_runtime.h>
#include <physics/physics_module.h>
#include <ui/ui_draw_list.h>
#include <ui/ui_layout.h>
#include <ui/ui_module.h>

#include "asset_pipeline/asset_database.h"
#include "asset_pipeline/build_settings.h"
#include "asset_pipeline/builtin_importers.h"
#include "asset_pipeline/file_utils.h"
#include "asset_pipeline/lua_stubs.h"
#include "asset_pipeline/packager.h"
#include "play/play_mode.h"
#include "scene/scene_editor.h"
#include "scene/scene_json.h"

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

			void set_source(AssetRef ref, const std::string& source)
			{
				for (LoadedScript& script : m_Scripts)
				{
					if (script.Asset == ref)
					{
						script.Source = source;
					}
				}
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
			std::vector<std::byte> Payload{};
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
					cooked.Payload.assign(payload.begin(), payload.end());
					if (header->Type == AssetType::Script)
					{
						cooked.Data = deserialize<ScriptData>(payload);
					}
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

				// a scene that uses the script imports with its property values and the script as a dependency (scripts import first)
				{
					SceneData level;
					level.Name = "level";
					EntityRecord entity;
					entity.Id = EntityGuid::generate();
					entity.Components.push_back(ComponentRecord{ script_component_name(player->Guid), { FieldRecord{ "label", std::string("boss") }, FieldRecord{ "speed", 9.0f } } });
					level.Entities.push_back(entity);
					write_text(temp / "assets" / "scenes" / "level.wscene", scene_to_json(level).dump(1));

					const std::optional<AssetRef> sceneAsset = database.ensure_imported(temp / "assets" / "scenes" / "level.wscene");
					check(sceneAsset.has_value(), "scene_with_a_script_component_imports");
					if (sceneAsset)
					{
						const Cooked cooked = read_cooked(database, *sceneAsset);
						const std::optional<SceneData> cookedScene = cooked.Found ? deserialize<SceneData>(cooked.Payload) : std::nullopt;
						check(cookedScene && cookedScene->Entities.size() == 1 && cookedScene->Entities[0].Components.size() == 1
							&& cookedScene->Entities[0].Components[0].Fields.size() == 2, "scene_with_a_script_component_keeps_its_fields");
						check(cooked.Found && std::find(cooked.Header.Dependencies.begin(), cooked.Header.Dependencies.end(), player->resolved()) != cooked.Header.Dependencies.end(), "scene_depends_on_its_scripts");
					}
				}


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

		float speed_of(const EntityRecord& entity, const std::string& component, float fallback = -1.0f)
		{
			for (const ComponentRecord& record : entity.Components)
			{
				if (record.Name == component)
				{
					for (const FieldRecord& field : record.Fields)
					{
						if (field.Name == "speed" && std::holds_alternative<float>(field.Value))
						{
							return std::get<float>(field.Value);
						}
					}
				}
			}
			return fallback;
		}

		void check_scene_scripts(Check& check)
		{
			const AssetRef mover{ AssetGuid::generate(), 0 };
			const AssetRef sceneScript{ AssetGuid::generate(), 0 };

			ScriptClass moverClass;
			moverClass.Asset = mover;
			moverClass.Name = "mover";
			FieldDescriptor speedField;
			speedField.Name = "speed";
			speedField.Type = FieldType::Float;
			speedField.Default = 1.0f;
			moverClass.Properties.push_back(speedField);
			register_script_component(moverClass);
			const std::string component = script_component_name(mover.Guid);

			SceneData data;
			data.Name = "scripts";
			data.Settings.Scripts = { sceneScript };
			EntityRecord entity;
			entity.Id = EntityGuid::generate();
			entity.Components.push_back(ComponentRecord{ component, { FieldRecord{ "speed", 7.5f } } });
			data.Entities.push_back(entity);

			const std::vector<std::byte> bytes = serialize(data);
			const std::optional<SceneData> back = deserialize<SceneData>(bytes);
			check(back && back->Settings.Scripts == data.Settings.Scripts && back->Entities.size() == 1, "scene_settings_scripts_roundtrip: binary");

			const SceneJsonResult parsed = scene_from_json(scene_to_json(data));
			check(parsed.Scene && parsed.Scene->Settings.Scripts == data.Settings.Scripts, "scene_settings_scripts_roundtrip: json");
			check(parsed.Scene && parsed.Scene->Entities.size() == 1 && speed_of(parsed.Scene->Entities[0], component) == 7.5f, "script_properties_roundtrip_in_scene: json");

			const std::vector<AssetRef> references = collect_references(data);
			const auto has = [&](AssetRef ref) { return std::find(references.begin(), references.end(), ref) != references.end(); };
			check(references.size() == 2 && has(mover) && has(sceneScript), "scene_dependencies_include_scripts");

			{
				Scene scene;
				apply_scene_data(scene, data);
				const ComponentInfo* info = ComponentRegistry::get().find(component);
				bool found = false;
				for (const entt::entity entity : scene.registry().view<ValueBags>())
				{
					const FieldValue value = info->Get(scene.registry(), entity, 0);
					found = std::holds_alternative<float>(value) && std::get<float>(value) == 7.5f;
				}
				check(found, "script_properties_roundtrip_in_scene: applied to a scene");
			}

			// a hundred entities with the same script keep their own values
			{
				Scene scene;
				std::vector<EntityGuid> ids;
				for (int i = 0; i < 100; i++)
				{
					EntityRecord record;
					record.Id = EntityGuid::generate();
					record.Components.push_back(ComponentRecord{ component, { FieldRecord{ "speed", static_cast<float>(i) } } });
					ids.push_back(record.Id);
					apply_record(scene, record);
				}
				const ComponentInfo* info = ComponentRegistry::get().find(component);
				bool allKept = true;
				for (int i = 0; i < 100; i++)
				{
					const entt::entity entity = scene.find(ids[i]);
					const FieldValue value = entity == entt::null ? FieldValue{} : info->Get(scene.registry(), entity, 0);
					allKept = allKept && std::holds_alternative<float>(value) && std::get<float>(value) == static_cast<float>(i);
				}
				check(allKept, "same_script_on_many_entities_keeps_each_value");
			}

			// a prefab instance can add the script to a node and override its property
			{
				PrefabData prefab;
				PrefabNode node;
				node.Name = "node";
				node.StableId = 1234;
				prefab.Nodes.push_back(node);

				EntityRecord instance;
				instance.Id = EntityGuid::generate();
				PrefabOverride add;
				add.Kind = OverrideKind::AddComponent;
				add.NodeId = 1234;
				add.Added = ComponentRecord{ component, { FieldRecord{ "speed", 2.0f } } };
				PrefabOverride set;
				set.Kind = OverrideKind::SetField;
				set.NodeId = 1234;
				set.Component = component;
				set.Field = "speed";
				set.Value = 7.5f;
				instance.Overrides = { add, set };

				const ExpandResult expanded = expand_prefab(instance, prefab);
				bool overridden = false;
				for (const EntityRecord& record : expanded.Entities)
				{
					overridden = overridden || speed_of(record, component) == 7.5f;
				}
				check(overridden, "prefab_override_of_script_property");
			}
		}

		// A scene with a runtime and the lifecycle running on it, for the lifecycle checks.
		struct Harness
		{
			MemorySource Source;
			ScriptRuntime Runtime{ Source };
			Ref<Scene> TheScene = CreateRef<Scene>();
			ScriptInstances Instances{ Runtime };
			std::vector<std::string> Events;

			Harness()
			{
				Runtime.sandbox().set_function("record", [this](const std::string& event) { Events.push_back(event); });
				Runtime.sandbox().set_function("kill", [this](const EntityHandle& handle)
					{
						const entt::entity entity = TheScene->find(handle.Guid);
						if (entity != entt::null)
						{
							TheScene->destroy(entity);
						}
					});
			}

			// Adds a script and registers its component.
			AssetRef add_script(const std::string& name, const std::string& source)
			{
				const AssetRef ref = Source.add("assets/scripts/" + name + ".lua", source);
				std::string error;
				ScriptClass* scriptClass = Runtime.load(ref, error);
				if (scriptClass == nullptr)
				{
					WI_SCRIPT_LOG(Error, "harness script {0}: {1}", name, error);
					return ref;
				}
				register_script_component(*scriptClass);
				return ref;
			}

			entt::entity spawn(const std::string& name, std::initializer_list<AssetRef> scripts)
			{
				const entt::entity entity = TheScene->create(name);
				for (const AssetRef& script : scripts)
				{
					attach(entity, script);
				}
				return entity;
			}

			void attach(entt::entity entity, AssetRef script)
			{
				ComponentRegistry::get().find(script_component_name(script.Guid))->Add(TheScene->registry(), entity);
			}

			void detach(entt::entity entity, AssetRef script)
			{
				ComponentRegistry::get().find(script_component_name(script.Guid))->Remove(TheScene->registry(), entity);
			}

			size_t count(const std::string& event) const { return static_cast<size_t>(std::count(Events.begin(), Events.end(), event)); }
		};

		void check_lifecycle(Check& check)
		{
			{
				Harness h;
				const AssetRef ref = h.add_script("order", R"(return Script:extend{
					on_init = function(self) record("init") end,
					on_update = function(self, dt) record("update") end,
					on_destroy = function(self) record("destroy") end })");
				const entt::entity entity = h.spawn("e", { ref });
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				h.Instances.update(0.016f);
				h.detach(entity, ref);
				h.Instances.update(0.016f);
				check(h.Events == std::vector<std::string>({ "init", "update", "update", "destroy" }), "hooks_run_in_order / component_removed_calls_on_destroy");
			}

			{
				Harness h;
				const AssetRef entityScript = h.add_script("entity_side", "return Script:extend{ on_update = function(self, dt) record('entity') end }");
				const AssetRef sceneScript = h.add_script("scene_side", "return Script:extend{ on_update = function(self, dt) record('scene') end }");
				h.spawn("e", { entityScript });
				SceneSettings settings = h.TheScene->settings();
				settings.Scripts = { sceneScript };
				h.TheScene->set_settings(settings);
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				check(h.Events == std::vector<std::string>({ "scene", "entity" }), "scene_scripts_run_before_entity_scripts");
			}

			{
				Harness h;
				const AssetRef child = h.add_script("child", "return Script:extend{ on_init = function(self) record('child_init') end }");
				const AssetRef spawner = h.add_script("spawner", "return Script:extend{ on_update = function(self, dt) spawn_child() end }");
				h.Runtime.sandbox().set_function("spawn_child", [&]()
					{
						if (h.count("spawned") == 0)
						{
							h.Events.push_back("spawned");
							h.spawn("child", { child });
						}
					});
				h.spawn("spawner", { spawner });
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				const bool notYet = h.count("child_init") == 0 && h.count("spawned") == 1;
				h.Instances.update(0.016f);
				check(notYet && h.count("child_init") == 1, "entity_created_by_script_inits_next_frame");
			}

			{
				Harness h;
				const AssetRef failing = h.add_script("failing", "return Script:extend{ on_update = function(self, dt) record('a_try') error('boom') end }");
				const AssetRef healthy = h.add_script("healthy", "return Script:extend{ on_update = function(self, dt) record('b') end }");
				h.spawn("a", { failing });
				h.spawn("b", { healthy });
				h.Instances.start(h.TheScene);
				for (int i = 0; i < 3; i++)
				{
					h.Instances.update(0.016f);
				}
				check(h.count("a_try") == 1 && h.count("b") == 3, "throwing_script_does_not_stop_others");
			}

			{
				Harness h;
				const AssetRef failing = h.add_script("init_fails", R"(return Script:extend{
					on_init = function(self) error('no init') end,
					on_update = function(self, dt) record('update_after_failed_init') end,
					on_destroy = function(self) record('destroy_after_failed_init') end })");
				const entt::entity entity = h.spawn("a", { failing });
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				h.detach(entity, failing);
				h.Instances.update(0.016f);
				check(h.Events.empty(), "failed_init_disables_the_instance");
			}

			{
				Harness h;
				const AssetRef suicide = h.add_script("suicide", R"(return Script:extend{
					on_update = function(self, dt) kill(self.entity) record('after_kill') end,
					on_destroy = function(self) record('suicide_destroy_valid=' .. tostring(self.entity:is_valid())) end })");
				const AssetRef victim = h.add_script("victim", "return Script:extend{ on_destroy = function(self) record('victim_destroy') end }");
				const AssetRef murderer = h.add_script("murderer", "return Script:extend{ on_update = function(self, dt) kill(target) end }");
				h.spawn("suicide", { suicide });
				const entt::entity victimEntity = h.spawn("victim", { victim });
				h.spawn("murderer", { murderer });
				const EntityGuid victimGuid = h.TheScene->registry().get<Id>(victimEntity).Guid;
				h.Instances.start(h.TheScene);
				h.Runtime.sandbox()["target"] = EntityHandle{ victimGuid, &h.Instances.context() };
				h.Instances.update(0.016f);
				h.Instances.update(0.016f);
				check(h.count("suicide_destroy_valid=false") == 1 && h.count("victim_destroy") == 1 && h.count("after_kill") == 1, "destroying_entities_in_update_is_safe");
			}

			{
				Harness h;
				const AssetRef ref = h.add_script("stop", "return Script:extend{ on_destroy = function(self) record('destroy') end }");
				h.spawn("a", { ref });
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				h.Instances.stop();
				h.Instances.stop();
				check(h.count("destroy") == 1 && !h.Instances.running(), "stop_destroys_every_instance_once");
			}
		}

		void check_api(Check& check)
		{
			{
				Harness h;
				const AssetRef ref = h.add_script("math", R"(return Script:extend{ on_init = function(self)
					local a, b = vec3(1, 2, 3), vec3(4, 5, 6)
					local c = a + b
					record(string.format('%g,%g,%g', c.x, c.y, c.z))
					record(string.format('%g', a:dot(b)))
					record(string.format('%g', (a * 2).y))
					record(string.format('%g', (2 * a).z))
					record(string.format('%g', vec3(3, 4, 0):length()))
					record(string.format('%g', a:cross(b).x))
					record(tostring(a == vec3(1, 2, 3)))
					local q = quat.from_axis_angle(vec3(0, 1, 0), 90)
					local v = q * vec3(1, 0, 0)
					record(tostring(math.abs(v.x) < 1e-5 and math.abs(v.y) < 1e-5 and math.abs(v.z + 1) < 1e-5))
					local m = mat4.translate(vec3(1, 2, 3)) * mat4.identity()
					local p = m:transform_point(vec3(0, 0, 0))
					record(string.format('%g,%g,%g', p.x, p.y, p.z))
				end })");
				h.spawn("e", { ref });
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				check(h.Events == std::vector<std::string>({ "5,7,9", "32", "4", "6", "5", "-3", "true", "true", "1,2,3" }), "vec3_quat_mat4_math");
			}

			{
				Harness h;
				const AssetRef ref = h.add_script("transform", R"(return Script:extend{ on_init = function(self)
					record(self.name)
					self.transform.position.x = 4
					record(string.format('%g', self.transform.position.x))
					self.transform.scale = vec3(2, 2, 2)
					self.transform.rotation = quat.from_axis_angle(vec3(0, 0, 1), 90)
					local ok, err = pcall(function() self.transform.position = 'nope' end)
					record(tostring(ok))
					record(err)
					local ok2, err2 = pcall(function() return self.transform.nothing end)
					record(tostring(ok2))
					record(err2)
				end })");
				const entt::entity entity = h.spawn("hero", { ref });
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				const Transform& transform = h.TheScene->registry().get<Transform>(entity);
				check(transform.Position.x == 4.0f && transform.Scale == glm::vec3(2.0f) && std::abs(transform.Rotation.z - std::sqrt(0.5f)) < 1e-4f, "component_proxy_reads_and_writes");
				check(h.Events.size() == 6 && h.Events[0] == "hero" && h.Events[1] == "4", "self_name_and_transform");
				check(h.Events.size() == 6 && h.Events[2] == "false" && contains(h.Events[3], "Position"), "wrong_type_write_errors");
				check(h.Events.size() == 6 && h.Events[4] == "false" && contains(h.Events[5], "nothing"), "unknown_field_errors");
			}

			{
				Harness h;
				const AssetRef ref = h.add_script("spawner2", R"(return Script:extend{ on_init = function(self)
					local e = create_entity('made', self.entity)
					record(tostring(e:is_valid()))
					record(tostring(find_entity('made') == e))
					record(tostring(e.parent == self.entity))
					record(tostring(#self.entity.children))
					destroy_entity(e)
					record(tostring(e:is_valid()))
					record(tostring(find_entity('made') == nil))
				end })");
				h.spawn("parent", { ref });
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				check(h.Events == std::vector<std::string>({ "true", "true", "true", "1", "false", "true" }), "entity_create_find_destroy_and_hierarchy");
			}

			{
				Harness h;
				const AssetRef enemy = h.add_script("Enemy", "return Script:extend{ properties = { health = { type = 'int', default = 3 } } }");
				const AssetRef hunter = h.add_script("hunter", R"(return Script:extend{ on_init = function(self)
					local other = find_entity('enemy')
					record(tostring(other:get('Enemy').health))
					other:get('Enemy').health = 7
					record(tostring(other:get('Enemy').health))
					record(tostring(other:has('Enemy')) .. tostring(other:has('Light')))
				end })");
				const entt::entity enemyEntity = h.spawn("enemy", { enemy });
				h.spawn("hunter", { hunter });
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				const ComponentInfo* info = ComponentRegistry::get().find(script_component_name(enemy.Guid));
				const FieldValue health = info->Get(h.TheScene->registry(), enemyEntity, 0);
				check(h.Events == std::vector<std::string>({ "3", "7", "truefalse" }) && std::holds_alternative<i32>(health) && std::get<i32>(health) == 7, "get_other_script_component_by_name");
			}

			{
				Harness h;
				const AssetRef a = h.Source.add("assets/a/dup.lua", "return Script:extend{}");
				const AssetRef b = h.Source.add("assets/b/dup.lua", "return Script:extend{}");
				for (const AssetRef& ref : { a, b })
				{
					std::string error;
					register_script_component(*h.Runtime.load(ref, error));
				}
				const AssetRef user = h.add_script("asker", R"(return Script:extend{ on_init = function(self)
					local ok, err = pcall(function() return self.entity:get('dup') end)
					record(tostring(ok))
					record(err)
				end })");
				const entt::entity entity = h.spawn("e", { user, a, b });
				(void)entity;
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				check(h.Events.size() == 2 && h.Events[0] == "false" && contains(h.Events[1], "assets/a/dup.lua") && contains(h.Events[1], "assets/b/dup.lua"), "ambiguous_script_name_errors");
			}

			{
				Harness h;
				const AssetRef ref = h.add_script("props", R"(return Script:extend{
					properties = { speed = { type = 'float', default = 5 } },
					on_update = function(self, dt)
						record(string.format('%g', self.speed))
						self.speed = 9
						local ok = pcall(function() self.speed = 'fast' end)
						record(tostring(ok))
						self.counter = (self.counter or 0) + 1
					end })");
				const entt::entity entity = h.spawn("e", { ref });
				const ComponentInfo* info = ComponentRegistry::get().find(script_component_name(ref.Guid));
				info->Set(h.TheScene->registry(), entity, 0, 2.0f);
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				const FieldValue afterScript = info->Get(h.TheScene->registry(), entity, 0);
				info->Set(h.TheScene->registry(), entity, 0, 11.0f);
				h.Instances.update(0.016f);
				check(h.Events == std::vector<std::string>({ "2", "false", "11", "false" }) && std::holds_alternative<float>(afterScript) && std::get<float>(afterScript) == 9.0f, "property_via_self_shares_bag");
			}
		}

		void check_engine_bindings(Check& check, AssetPipelineModule& pipeline)
		{
			{
				Harness h;
				DebugDraw debug;
				h.Instances.context().Debug = &debug;
				const AssetRef ref = h.add_script("drawer", R"(return Script:extend{ on_update = function(self, dt)
					debug_draw.line(vec3(0, 0, 0), vec3(1, 0, 0), vec4(1, 0, 0, 1))
					debug_draw.box(vec3(-1), vec3(1))
					debug_draw.sphere(vec3(0, 2, 0), 0.5)
					record(string.format('%g', time.delta()))
					record(string.format('%g', time.total()))
				end })");
				h.spawn("e", { ref });
				h.Instances.start(h.TheScene);
				h.Instances.update(0.5f);
				h.Instances.update(0.25f);
				const DebugDrawData drawn = debug.seal(0.0f);
				check(!drawn.Lines[0].empty() && h.Events == std::vector<std::string>({ "0.5", "0.5", "0.25", "0.75" }), "debug_draw_and_time_bindings");
			}

			{
				Harness h;
				const entt::entity first = h.TheScene->create("first");
				const entt::entity second = h.TheScene->create("second");
				h.TheScene->registry().emplace<Camera>(first).Primary = true;
				h.TheScene->registry().emplace<Camera>(second);
				const AssetRef ref = h.add_script("cameras", R"(return Script:extend{ on_init = function(self)
					record(camera.primary().name)
					camera.set_primary(find_entity('second'))
					record(camera.primary().name)
				end })");
				h.spawn("e", { ref });
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				check(h.Events == std::vector<std::string>({ "first", "second" }) && !h.TheScene->registry().get<Camera>(first).Primary && h.TheScene->registry().get<Camera>(second).Primary, "camera_primary_roundtrip");
			}

			{
				Harness h;
				const AssetRef ref = h.add_script("no_engine", R"(return Script:extend{ on_init = function(self)
					local ok, err = pcall(load_asset, 'assets/none.png')
					record(tostring(ok))
					record(err)
					local ok2, err2 = pcall(load_scene, 'assets/none.wscene')
					record(tostring(ok2))
				end })");
				h.spawn("e", { ref });
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				check(h.Events.size() == 3 && h.Events[0] == "false" && h.Events[2] == "false", "asset_and_scene_calls_error_without_the_modules");
			}

			// the real modules: a path from the catalog resolves, an unknown one is an error, and a scene load can be followed
			AssetManager* assets = Engine::get().get_module<AssetManager>();
			SceneModule* scenes = Engine::get().get_module<SceneModule>();
			(void)pipeline;
			if (assets != nullptr && scenes != nullptr)
			{
				Harness h;
				h.Instances.context().Assets = assets;
				h.Instances.context().Scenes = scenes;

				const std::vector<AssetCatalog::Entry> entries = assets->catalog_entries();
				const std::string known = entries.empty() ? std::string() : entries.front().Path;
				h.Runtime.sandbox()["known_path"] = known;
				sol::object kept;
				h.Runtime.sandbox().set_function("keep", [&kept](sol::object object) { kept = object; });
				bool sceneAvailable = false;
				for (const AssetCatalog::Entry& entry : entries)
				{
					sceneAvailable = sceneAvailable || entry.Path.ends_with(".wscene");
					if (entry.Path.ends_with(".wscene"))
					{
						h.Runtime.sandbox()["scene_path"] = entry.Path;
						break;
					}
				}

				const AssetRef ref = h.add_script("real", R"(return Script:extend{ on_init = function(self)
					local asset = load_asset(known_path)
					record(tostring(asset:is_valid()))
					record(tostring(asset.guid ~= ''))
					local ok = pcall(load_asset, 'assets/definitely/not/here.png')
					record(tostring(ok))
					if scene_path then keep(load_scene(scene_path, 'additive')) end
				end })");
				h.spawn("e", { ref });
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				check(!known.empty() && h.Events.size() >= 3 && h.Events[0] == "true" && h.Events[1] == "true" && h.Events[2] == "false", "load_asset_resolves_catalog_paths");

				if (sceneAvailable)
				{
					bool finished = false;
					for (int i = 0; i < 600 && !finished && kept.valid() && kept.is<LuaSceneLoad>(); i++)
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(50));
						finished = kept.as<LuaSceneLoad&>().Progress->finished();
					}
					check(finished, "load_scene_from_script");
				}
				else
				{
					WI_SCRIPT_LOG(Info, "load_scene_from_script skipped: no scene in the catalog");
				}
			}
		}

		void check_scene_changes_and_reload(Check& check)
		{
			{
				Harness h;
				const AssetRef ref = h.add_script("swap", R"(return Script:extend{
					on_init = function(self) record('init') end,
					on_destroy = function(self) record('destroy') end })");
				h.spawn("old", { ref });
				Ref<Scene> next = CreateRef<Scene>();
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				// what a replaced scene looks like to the lifecycle: the module stops it and starts the next one
				h.Instances.stop();
				const size_t destroyedOnce = h.count("destroy");
				h.TheScene = next;
				h.spawn("new", { ref });
				h.Instances.start(next);
				h.Instances.update(0.016f);
				check(destroyedOnce == 1 && h.count("destroy") == 1 && h.count("init") == 2, "replace_load_destroys_old_scripts_once");
			}

			{
				Harness h;
				const AssetRef ref = h.add_script("extra", "return Script:extend{ on_init = function(self) record('init') end }");
				h.spawn("first", { ref });
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				h.spawn("added by an additive load", { ref });
				h.Instances.update(0.016f);
				check(h.count("init") == 2, "additive_load_discovers_new_scripts");
			}

			{
				Harness h;
				const AssetRef ref = h.add_script("hot", R"(return Script:extend{
					on_update = function(self, dt) self.counter = (self.counter or 0) + 1 record('old' .. self.counter) end })");
				h.spawn("e", { ref });
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				h.Source.set_source(ref, R"(return Script:extend{
					on_update = function(self, dt) self.counter = (self.counter or 0) + 1 record('new' .. self.counter) end })");
				std::string error;
				const bool reloaded = h.Runtime.reload_script(ref, error, &h.TheScene->registry());
				h.Instances.update(0.016f);
				check(reloaded && h.Events == std::vector<std::string>({ "old1", "new2" }), "reload_swaps_methods_in_place");
			}

			{
				Harness h;
				const AssetRef ref = h.add_script("keeps", "return Script:extend{ on_update = function(self, dt) record('works') end }");
				h.spawn("e", { ref });
				h.Instances.start(h.TheScene);
				h.Source.set_source(ref, "return Script:extend{ on_update = ");
				std::string error;
				const bool reloaded = h.Runtime.reload_script(ref, error, &h.TheScene->registry());
				h.Instances.update(0.016f);
				check(!reloaded && contains(error, "assets/scripts/keeps.lua") && h.Events == std::vector<std::string>({ "works" }), "reload_with_syntax_error_keeps_old_class");
			}

			{
				Harness h;
				const AssetRef util = h.Source.add("assets/scripts/shared.lua", "return { value = 1 }");
				const AssetRef user = h.add_script("uses_shared", R"(
					local shared = require('assets/scripts/shared.lua')
					return Script:extend{ on_update = function(self, dt) record(tostring(shared.value)) end })");
				h.spawn("e", { user });
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				h.Source.set_source(util, "return { value = 2 }");
				std::string error;
				const bool reloaded = h.Runtime.reload_script(util, error, &h.TheScene->registry());
				h.Instances.update(0.016f);
				check(reloaded && h.Events == std::vector<std::string>({ "1", "2" }), "reload_updates_dependents");
			}

			{
				Harness h;
				const AssetRef ref = h.add_script("bag", "return Script:extend{ properties = { kept = { type = 'int', default = 1 }, gone = { type = 'string', default = 'x' } } }");
				const entt::entity entity = h.spawn("e", { ref });
				const ComponentInfo* before = ComponentRegistry::get().find(script_component_name(ref.Guid));
				before->Set(h.TheScene->registry(), entity, static_cast<size_t>(before->field_index("kept")), 5);
				h.Source.set_source(ref, "return Script:extend{ properties = { kept = { type = 'int', default = 1 }, added = { type = 'float', default = 2.5 } } }");
				std::string error;
				const bool reloaded = h.Runtime.reload_script(ref, error, &h.TheScene->registry());
				const ComponentInfo* after = ComponentRegistry::get().find(script_component_name(ref.Guid));
				const FieldValue kept = after->Get(h.TheScene->registry(), entity, static_cast<size_t>(after->field_index("kept")));
				const FieldValue added = after->Get(h.TheScene->registry(), entity, static_cast<size_t>(after->field_index("added")));
				check(reloaded && after->field_index("gone") < 0 && std::holds_alternative<i32>(kept) && std::get<i32>(kept) == 5
					&& std::holds_alternative<float>(added) && std::get<float>(added) == 2.5f, "reload_reconciles_bags");
			}

			{
				Harness h;
				const AssetRef base = h.add_script("base_class", "return Script:extend{ speak = function(self) return 'base1' end }");
				const AssetRef child = h.add_script("child_class", R"(
					local Base = require('assets/scripts/base_class.lua')
					return Base:extend{ on_update = function(self, dt) record(self:speak()) end })");
				h.spawn("e", { child });
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				h.Source.set_source(base, "return Script:extend{ speak = function(self) return 'base2' end }");
				std::string error;
				check(h.Runtime.reload_script(base, error, &h.TheScene->registry()), "reload_inherited_class: reloads");
				h.Instances.update(0.016f);
				h.Source.set_source(base, "return Script:extend{ speak = function(self) return 'base3' end }");
				check(h.Runtime.reload_script(base, error, &h.TheScene->registry()), "reload_inherited_class: reloads again");
				h.Instances.update(0.016f);
				check(h.Events == std::vector<std::string>({ "base1", "base2", "base3" }), "reload_inherited_class_reaches_instances");
			}
		}

		// A game: the scripts are only in the pack, the sources are gone.
		void check_pak(Check& check)
		{
			std::error_code error;
			const std::filesystem::path temp = std::filesystem::temp_directory_path() / "wiwa_script_pak_smoke";
			std::filesystem::remove_all(temp, error);

			const std::filesystem::path scripts = temp / "assets" / "scripts";
			const std::string root = (temp / "assets").generic_string();
			write_text(scripts / "pak_lib.lua", "return { answer = 42 }");
			write_text(scripts / "pak_main.lua", "local lib = require('" + root + "/scripts/pak_lib.lua')\nreturn Script:extend{\n"
				"  properties = { speed = { type = 'float', default = 1 } },\n"
				"  on_init = function(self) record('init ' .. lib.answer) end,\n"
				"  on_update = function(self, dt) record(string.format('update %g', self.speed)) end }");
			write_text(scripts / "pak_scene_script.lua", "return Script:extend{ on_update = function(self, dt) record('scene script') end }");
			write_text(scripts / "unused.lua", "return Script:extend{}");

			AssetDatabase database(temp / "assets", temp / "library");
			register_builtin_importers(database);
			database.scan();

			const std::optional<AssetRef> mainScript = database.ensure_imported(scripts / "pak_main.lua");
			const std::optional<AssetRef> sceneScript = database.ensure_imported(scripts / "pak_scene_script.lua");
			check(mainScript && sceneScript, "pak: scripts import");
			if (!mainScript || !sceneScript)
			{
				return;
			}

			SceneData level;
			level.Name = "pak_level";
			level.Settings.Scripts = { *sceneScript };
			EntityRecord entity;
			entity.Id = EntityGuid::generate();
			entity.Components.push_back(ComponentRecord{ script_component_name(mainScript->Guid), { FieldRecord{ "speed", 3.0f } } });
			level.Entities.push_back(entity);
			write_text(temp / "assets" / "scenes" / "pak_level.wscene", scene_to_json(level).dump(1));
			const std::optional<AssetRef> sceneAsset = database.ensure_imported(temp / "assets" / "scenes" / "pak_level.wscene");
			check(sceneAsset.has_value(), "pak: the scene imports");
			if (!sceneAsset)
			{
				return;
			}

			BuildSettings settings;
			settings.Scenes = { sceneAsset->Guid };
			settings.DefaultScene = sceneAsset->Guid;
			check(Packager::pack(database, temp / "out", nullptr, &settings, temp / "none_physics.wsettings", temp / "none_input.wsettings"), "pak: packing the build");

			// the sources are gone: only the pack is left
			std::filesystem::remove_all(temp / "assets", error);
			std::filesystem::remove_all(temp / "library", error);

			AssetManager manager;
			manager.init();
			manager.set_source(CreateScope<PakSource>(temp / "out" / "assets.wpak"));

			check(!manager.find_asset(root + "/scripts/unused.lua").has_value(), "unreferenced_script_not_packed");
			check(manager.find_asset(root + "/scripts/pak_lib.lua").has_value(), "pak: a required script is packed");

			AssetScriptSource source(manager);
			ScriptRuntime runtime(source);
			std::vector<std::string> events;
			runtime.sandbox().set_function("record", [&events](const std::string& event) { events.push_back(event); });

			// what ScriptingModule does at start-up
			for (const AssetCatalog::Entry& entry : manager.catalog_entries())
			{
				if (entry.Path.ends_with(".lua"))
				{
					std::string loadError;
					ScriptClass* scriptClass = runtime.load(AssetRef{ entry.Guid, 0 }, loadError);
					check(scriptClass != nullptr, "pak: " + entry.Path + " loads " + loadError);
					if (scriptClass != nullptr)
					{
						register_script_component(*scriptClass);
					}
				}
			}

			Handle<SceneData> sceneHandle = manager.load<SceneData>(*sceneAsset);
			manager.wait_idle();
			check(sceneHandle.get() != nullptr, "pak: the scene loads");
			if (sceneHandle.get() != nullptr)
			{
				Ref<Scene> scene = CreateRef<Scene>();
				apply_scene_data(*scene, *sceneHandle.get());
				ScriptInstances instances(runtime);
				instances.start(scene);
				instances.update(0.016f);
				check(events == std::vector<std::string>({ "init 42", "update 3", "scene script" }) || events == std::vector<std::string>({ "init 42", "scene script", "update 3" }), "pak_runs_scripts_without_sources");
				instances.stop();
			}

			manager.shutdown();
			std::filesystem::remove_all(temp, error);
		}

		// Findings of the final review, each pinned by a test.
		void check_review_fixes(Check& check)
		{
			{
				MemorySource source;
				const AssetRef ref = source.add("assets/scripts/pcall_loop.lua", R"(return Script:extend{
					on_update = function() while true do pcall(function() while true do end end) end end })");
				ScriptRuntime runtime(source);
				std::string error;
				const ScriptClass* cls = runtime.load(ref, error);
				runtime.set_instruction_budget(100000);
				bool stopped = false;
				if (cls != nullptr)
				{
					sol::protected_function_result result = runtime.call(sol::protected_function(cls->Class["on_update"]));
					stopped = !result.valid();
				}
				check(stopped, "pcall_cannot_defeat_the_budget");
			}

			{
				MemorySource source;
				const AssetRef ref = source.add("assets/scripts/c_loop.lua", R"(return Script:extend{
					on_update = function() while true do noop() end end })");
				ScriptRuntime runtime(source);
				std::string error;
				const ScriptClass* cls = runtime.load(ref, error);
				runtime.sandbox().set_function("noop", []() {});
				runtime.set_instruction_budget(100000);
				bool stopped = false;
				if (cls != nullptr)
				{
					sol::protected_function_result result = runtime.call(sol::protected_function(cls->Class["on_update"]));
					stopped = !result.valid();
				}
				check(stopped, "loop_calling_a_c_function_is_stopped");
			}

			{
				MemorySource source;
				const AssetRef ref = source.add("assets/scripts/nested_loop.lua", R"(return Script:extend{
					on_update = function() while true do inner() end end })");
				ScriptRuntime runtime(source);
				std::string error;
				const ScriptClass* cls = runtime.load(ref, error);
				sol::protected_function quick = runtime.state().load("return 1").get<sol::protected_function>();
				runtime.sandbox().set_function("inner", [&runtime, quick]() { runtime.call(quick); });
				runtime.set_instruction_budget(100000);
				bool stopped = false;
				if (cls != nullptr)
				{
					sol::protected_function_result result = runtime.call(sol::protected_function(cls->Class["on_update"]));
					stopped = !result.valid();
				}
				check(stopped, "nested_call_does_not_reset_the_budget");
			}

			{
				MemorySource source;
				const AssetRef ref = source.add("assets/scripts/trap.lua", "return setmetatable({}, { __index = function() error('trap') end })");
				ScriptRuntime runtime(source);
				std::string error;
				check(runtime.load(ref, error) == nullptr && !error.empty(), "metamethod_error_is_a_load_error");
			}

			{
				ComponentInfo first = make_value_bag_info("review:replaced", { FieldDescriptor{ "a", FieldType::Float, {}, 1.0f }, FieldDescriptor{ "b", FieldType::Float, {}, 1.0f } });
				ComponentRegistry::get().register_component(std::move(first));
				const ComponentInfo* held = ComponentRegistry::get().find("review:replaced");
				ComponentRegistry::get().register_component(make_value_bag_info("review:replaced", { FieldDescriptor{ "only", FieldType::Float, {}, 1.0f } }));
				check(held != nullptr && held->Fields.size() == 2 && held->Fields[0].Name == "a", "registry_replace_keeps_the_old_info_alive");
			}

			{
				ComponentRegistry::get().register_component(make_value_bag_info("review:retyped", { FieldDescriptor{ "v", FieldType::Float, {}, 1.0f } }));
				entt::registry registry;
				const entt::entity entity = registry.create();
				const ComponentInfo* before = ComponentRegistry::get().find("review:retyped");
				before->Add(registry, entity);
				before->Set(registry, entity, 0, 3.0f);
				ComponentRegistry::get().register_component(make_value_bag_info("review:retyped", { FieldDescriptor{ "v", FieldType::String, {}, std::string("d") } }));
				const FieldValue value = ComponentRegistry::get().find("review:retyped")->Get(registry, entity, 0);
				check(std::holds_alternative<std::string>(value) && std::get<std::string>(value) == "d", "retyped_bag_value_reads_as_the_default");
			}

			{
				Harness h;
				const AssetRef ref = h.add_script("mandatory", R"(return Script:extend{ on_init = function(self)
					record(tostring(pcall(function() self.entity:remove('Name') end)))
					record(tostring(pcall(function() self.entity:remove('Id') end)))
					record(tostring(pcall(function() self.entity:remove('Transform') end)))
					record(tostring(pcall(function() self.entity:add('Name') end)))
					record(self.name)
				end })");
				h.spawn("hero", { ref });
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				check(h.Events == std::vector<std::string>({ "false", "false", "false", "false", "hero" }), "scripts_cannot_remove_mandatory_components");
			}

			{
				MemorySource source;
				const AssetRef ref = source.add("assets/scripts/binary.lua", std::string("\x1bLua\x54\x00junk", 10));
				ScriptRuntime runtime(source);
				std::string error;
				check(runtime.load(ref, error) == nullptr && !error.empty(), "bytecode_source_is_rejected");
			}

			{
				Harness h;
				const AssetRef ref = h.Source.add("assets/scripts/never_loads.lua", "tick() error('top level')");
				ScriptClass fake;
				fake.Asset = ref;
				fake.Name = "never_loads";
				register_script_component(fake);
				int ticks = 0;
				h.Runtime.sandbox().set_function("tick", [&ticks]() { ticks++; });
				h.spawn("a", { ref });
				h.spawn("b", { ref });
				h.Instances.start(h.TheScene);
				for (int i = 0; i < 3; i++)
				{
					h.Instances.update(0.016f);
				}
				check(ticks == 1, "failing_script_is_not_run_again_every_frame");
			}

			{
				Harness h;
				const AssetRef ref = h.add_script("numbers", R"(return Script:extend{
					properties = { speed = { type = 'float', default = 1 }, count = { type = 'int', default = 1 } },
					on_init = function(self)
						record(tostring(pcall(function() self.transform.position = vec3(0 / 0, 0, 0) end)))
						record(tostring(pcall(function() self.speed = 0 / 0 end)))
						record(tostring(pcall(function() self.count = 1e20 end)))
						record(tostring(pcall(function() self.count = 7 end)))
					end })");
				h.spawn("e", { ref });
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				check(h.Events == std::vector<std::string>({ "false", "false", "false", "true" }), "non_finite_and_out_of_range_numbers_are_rejected");
			}

			{
				Harness h;
				sol::object kept;
				h.Runtime.sandbox().set_function("keep", [&kept](sol::object object) { kept = object; });
				const AssetRef ref = h.add_script("stale", "return Script:extend{ on_init = function(self) keep(self.transform) end }");
				h.spawn("e", { ref });
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				h.Instances.stop();
				h.Runtime.sandbox()["stale_proxy"] = kept;
				sol::environment env(h.Runtime.state(), sol::create, h.Runtime.sandbox());
				sol::protected_function_result result = h.Runtime.state().safe_script("return stale_proxy.position", env, sol::script_pass_on_error);
				check(!result.valid(), "stale_proxy_after_stop_is_an_error");
			}
		}

		// Play mode, task 1: the game clock scripts take their dt from.
		void check_time(Check& check)
		{
			{
				Time t;
				t.init();
				check(t.get_game_clock().IsPlaying && t.get_game_clock().TimeScale == 1.0f, "game_clock_starts_with_init");
			}

			{
				Time t;
				t.init();
				t.update();
				t.update();
				const bool first = t.get_delta_time_seconds() < 1.0f;
				t.stop_game();
				t.play_game();
				t.update();
				check(first && t.get_delta_time_seconds() < 1.0f, "game_clock_first_delta_is_small");
			}

			{
				Time t;
				t.init();
				t.update();
				t.pause_game();
				const float paused = t.get_game_clock().TimeSinceClock.count();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
				t.update();
				const bool frozen = t.get_game_clock().TimeSinceClock.count() == paused;
				t.unpause_game();
				std::this_thread::sleep_for(std::chrono::milliseconds(20));
				t.update();
				check(frozen && t.get_game_clock().TimeSinceClock.count() > paused, "pause_stops_the_game_clock");
			}

			{
				Time t;
				t.init();
				t.update();
				t.pause_game();
				std::this_thread::sleep_for(std::chrono::milliseconds(150));
				t.unpause_game();
				t.update();
				check(t.get_delta_time_seconds() < 0.05f && t.get_game_clock().TimeSinceClock.count() < 0.1f, "resume_does_not_deliver_the_time_spent_paused");
			}
		}

		bool entity_records_equal(const EntityRecord& a, const EntityRecord& b)
		{
			if (a.Id != b.Id || a.Parent != b.Parent || a.Components.size() != b.Components.size() || a.Overrides.size() != b.Overrides.size())
			{
				return false;
			}
			for (size_t i = 0; i < a.Components.size(); i++)
			{
				if (a.Components[i].Name != b.Components[i].Name || a.Components[i].Fields.size() != b.Components[i].Fields.size())
				{
					return false;
				}
				for (size_t f = 0; f < a.Components[i].Fields.size(); f++)
				{
					if (a.Components[i].Fields[f].Name != b.Components[i].Fields[f].Name || !(a.Components[i].Fields[f].Value == b.Components[i].Fields[f].Value))
					{
						return false;
					}
				}
			}
			for (size_t i = 0; i < a.Overrides.size(); i++)
			{
				if (a.Overrides[i].Kind != b.Overrides[i].Kind || a.Overrides[i].NodeId != b.Overrides[i].NodeId || a.Overrides[i].Field != b.Overrides[i].Field
					|| !(a.Overrides[i].Value == b.Overrides[i].Value))
				{
					return false;
				}
			}
			return true;
		}

		// Everything a scene holds that the clone must keep: the records in order (hierarchy, siblings, roots, reflected fields, overrides),
		// the settings, and what the records do not hold.
		bool scenes_equal(Scene& a, Scene& b)
		{
			const SceneData da = data_from_scene(a, "s");
			const SceneData db = data_from_scene(b, "s");
			if (da.Entities.size() != db.Entities.size() || !(da.Settings == db.Settings) || a.roots().size() != b.roots().size())
			{
				return false;
			}

			for (size_t i = 0; i < da.Entities.size(); i++)
			{
				if (!entity_records_equal(da.Entities[i], db.Entities[i]))
				{
					return false;
				}

				const entt::entity ea = a.find(da.Entities[i].Id);
				const entt::entity eb = b.find(da.Entities[i].Id);
				const PrefabChild* pa = a.registry().try_get<PrefabChild>(ea);
				const PrefabChild* pb = b.registry().try_get<PrefabChild>(eb);
				if ((pa == nullptr) != (pb == nullptr) || (pa != nullptr && (pa->Instance != pb->Instance || pa->NodeId != pb->NodeId)))
				{
					return false;
				}

				const UnknownComponents* ua = a.registry().try_get<UnknownComponents>(ea);
				const UnknownComponents* ub = b.registry().try_get<UnknownComponents>(eb);
				if ((ua == nullptr) != (ub == nullptr) || (ua != nullptr && (ua->Records.size() != ub->Records.size() || ua->Records[0].Name != ub->Records[0].Name)))
				{
					return false;
				}

				const ValueBags* ba = a.registry().try_get<ValueBags>(ea);
				const ValueBags* bb = b.registry().try_get<ValueBags>(eb);
				if ((ba == nullptr) != (bb == nullptr) || (ba != nullptr && (ba->Bags.size() != bb->Bags.size() || ba->Bags[0].Name != bb->Bags[0].Name || !(ba->Bags[0].Values == bb->Bags[0].Values))))
				{
					return false;
				}
			}
			for (size_t i = 0; i < a.roots().size(); i++)
			{
				if (a.registry().get<Id>(a.roots()[i]).Guid != b.registry().get<Id>(b.roots()[i]).Guid)
				{
					return false;
				}
			}
			return true;
		}

		void check_clone(Check& check)
		{
			ScriptClass scriptClass;
			scriptClass.Asset = AssetRef{ AssetGuid::generate(), 0 };
			scriptClass.Name = "cloned";
			FieldDescriptor speed;
			speed.Name = "speed";
			speed.Type = FieldType::Float;
			speed.Default = 1.0f;
			scriptClass.Properties.push_back(speed);
			register_script_component(scriptClass);

			Scene source;
			const entt::entity rootA = source.create("rootA");
			const entt::entity childA1 = source.create("childA1", rootA);
			source.create("childA2", rootA);
			const entt::entity grandchild = source.create("grandchild", childA1);
			const entt::entity rootB = source.create("rootB");
			const entt::entity rootC = source.create("rootC");

			source.registry().get<Transform>(grandchild).Position = glm::vec3(1.0f, 2.0f, 3.0f);
			Light& light = source.registry().emplace<Light>(rootB);
			light.Intensity = 7.0f;
			source.registry().emplace<Camera>(rootC).FovY = 33.0f;

			ComponentRegistry::get().find(script_component_name(scriptClass.Asset.Guid))->Add(source.registry(), rootB);
			ComponentRegistry::get().find(script_component_name(scriptClass.Asset.Guid))->Set(source.registry(), rootB, 0, 7.5f);

			PrefabInstance& instance = source.registry().emplace<PrefabInstance>(rootC);
			instance.Prefab = AssetRef{ AssetGuid::generate(), 0 };
			PrefabOverride override;
			override.Kind = OverrideKind::SetField;
			override.NodeId = 5;
			override.Component = "Transform";
			override.Field = "Position";
			override.Value = glm::vec3(4.0f);
			instance.Overrides.push_back(override);
			const entt::entity generated = source.create("generated", rootC);
			source.registry().emplace<PrefabChild>(generated, PrefabChild{ source.registry().get<Id>(rootC).Guid, 99 });

			ComponentRecord unknown;
			unknown.Name = "FromTheFuture";
			unknown.Fields.push_back(FieldRecord{ "x", 1.0f });
			source.registry().emplace<UnknownComponents>(rootA, UnknownComponents{ { unknown } });

			SceneSettings settings = source.settings();
			settings.Exposure.Auto = true;
			settings.Scripts = { scriptClass.Asset };
			source.set_settings(settings);
			source.update_world_transforms();

			Ref<Scene> reference = clone_scene(source);
			Ref<Scene> clone = clone_scene(source);
			check(clone != nullptr && scenes_equal(source, *clone), "clone_scene_is_exact");
			check(clone != nullptr && clone->registry().get<WorldTransform>(clone->find(source.registry().get<Id>(grandchild).Guid)).Matrix == source.registry().get<WorldTransform>(grandchild).Matrix, "clone_scene_computes_world_transforms");

			// changing the clone leaves the source alone
			clone->registry().get<Transform>(clone->find(source.registry().get<Id>(grandchild).Guid)).Position = glm::vec3(9.0f);
			clone->destroy(clone->find(source.registry().get<Id>(rootB).Guid));
			clone->create("only in the clone");
			clone->registry().get<PrefabChild>(clone->find(source.registry().get<Id>(generated).Guid)).NodeId = 1;
			check(scenes_equal(source, *reference), "changing_a_clone_leaves_the_source_alone");
			check(!scenes_equal(source, *clone), "the_scene_comparison_sees_a_difference");
		}

		// Play mode, task 3: what the editor keeps of its document, and what it refuses while the scene is running.
		void check_editor_document(Check& check)
		{
			AssetManager* assets = Engine::get().get_module<AssetManager>();
			SceneModule* scenes = Engine::get().get_module<SceneModule>();
			if (assets == nullptr || scenes == nullptr)
			{
				WI_SCRIPT_LOG(Info, "editor_document checks skipped: no AssetManager or SceneModule");
				return;
			}

			SceneEditor editor(*assets, *scenes);
			Ref<Scene> first = CreateRef<Scene>();
			editor.set_scene(first, "some/path.wscene");
			const entt::entity entity = editor.create_entity(entt::null);
			editor.select(entity);
			SceneSettings settings = editor.settings();
			settings.Exposure.Auto = true;
			editor.edit_settings(settings);

			const EditorDocument document = editor.capture_document();
			check(document.Path == "some/path.wscene" && document.Dirty && document.Selected.valid(), "editor_document_is_captured");

			editor.set_scene(CreateRef<Scene>());
			editor.restore_document(first, document);
			check(editor.get_scene() == first && editor.path() == "some/path.wscene" && editor.dirty() && editor.selected() == entity, "editor_document_roundtrips");

			editor.set_playing(true);
			check(editor.playing(), "editor_reports_playing");
			const std::filesystem::path file = std::filesystem::temp_directory_path() / "wiwa_play_gate.wscene";
			std::error_code error;
			std::filesystem::remove(file, error);
			const bool saved = editor.save_as(file);
			const bool savedToPath = editor.save();
			editor.new_scene();
			editor.open("assets/none.wscene");
			check(!saved && !savedToPath && !std::filesystem::exists(file) && editor.get_scene() == first && !editor.is_opening() && editor.path() == "some/path.wscene",
				"editor_refuses_file_actions_while_playing");
			editor.set_playing(false);
		}

		// A host that only writes down what PlayMode asks of it.
		class FakeHost final : public IPlayHost
		{
		public:
			Ref<Scene> Edit = CreateRef<Scene>();
			Ref<Scene> Active{};
			Ref<Scene> LastRestored{};
			bool CanPlay = true;
			EditorDocument Document{ "edit.wscene", true, {} };
			std::vector<std::string> Calls;

			FakeHost()
			{
				Edit->create("one");
				Edit->create("two");
				Active = Edit;
			}

			virtual Ref<Scene> edit_scene() override { return Edit; }
			virtual bool can_play() override { return CanPlay; }
			virtual EditorDocument capture_document() override { Calls.push_back("capture_document"); return Document; }
			virtual void show_scene(Ref<Scene> scene, bool playing) override
			{
				Calls.push_back(playing ? "show_scene(play)" : "show_scene(edit)");
				Active = scene;
			}
			virtual void restore_document(Ref<Scene> scene, const EditorDocument& document) override
			{
				Calls.push_back("restore_document");
				LastRestored = scene;
				(void)document;
			}
			virtual Ref<Scene> active_scene() override { return Active; }
			virtual void start_physics() override { Calls.push_back("start_physics"); }
			virtual void stop_physics() override { Calls.push_back("stop_physics"); }
			virtual void start_scripts() override { Calls.push_back("start_scripts"); }
			virtual void stop_scripts() override { Calls.push_back("stop_scripts"); }
			virtual void cancel_loads() override { Calls.push_back("cancel_loads"); }
			virtual void time_play() override { Calls.push_back("time_play"); }
			virtual void time_stop() override { Calls.push_back("time_stop"); }
			virtual void time_pause(bool paused) override { Calls.push_back(paused ? "time_pause(true)" : "time_pause(false)"); }

			size_t count(const std::string& call) const { return static_cast<size_t>(std::count(Calls.begin(), Calls.end(), call)); }
		};

		void check_play_mode(Check& check)
		{
			using Calls = std::vector<std::string>;

			{
				FakeHost host;
				PlayMode mode(host);
				mode.play();
				check(host.Calls == Calls({ "capture_document", "show_scene(play)", "time_play", "start_physics", "start_scripts" }) && mode.state() == PlayState::Playing, "play_from_edit");
				check(mode.play_scene() != nullptr && mode.play_scene() != host.Edit && mode.play_scene()->roots().size() == host.Edit->roots().size(), "play_runs_on_a_clone");
			}

			{
				FakeHost host;
				host.CanPlay = false;
				PlayMode mode(host);
				mode.play();
				check(host.Calls.empty() && mode.state() == PlayState::Edit, "play_refused_when_the_host_says_no");
			}

			{
				FakeHost host;
				PlayMode mode(host);
				mode.pause();
				mode.resume();
				mode.step();
				mode.stop();
				mode.update();
				check(host.Calls.empty() && mode.state() == PlayState::Edit, "controls_are_no_ops_in_edit");
			}

			{
				FakeHost host;
				PlayMode mode(host);
				mode.play();
				host.Calls.clear();
				mode.play();
				mode.resume();
				mode.step();
				mode.update();
				check(host.Calls.empty() && mode.state() == PlayState::Playing, "play_twice_resume_and_step_while_playing_do_nothing");
			}

			{
				FakeHost host;
				PlayMode mode(host);
				mode.play();
				host.Calls.clear();
				mode.pause();
				mode.pause();
				const bool paused = mode.state() == PlayState::Paused;
				mode.resume();
				check(paused && host.Calls == Calls({ "time_pause(true)", "time_pause(false)" }) && mode.state() == PlayState::Playing, "pause_and_resume");
			}

			{
				FakeHost host;
				PlayMode mode(host);
				mode.play();
				mode.pause();
				host.Calls.clear();
				mode.step();
				mode.update();
				const Calls afterFirst = host.Calls;
				mode.update();
				const Calls afterSecond = host.Calls;
				mode.update();
				mode.update();
				check(afterFirst == Calls({ "time_pause(false)" }) && afterSecond == Calls({ "time_pause(false)", "time_pause(true)" }) && host.Calls == afterSecond
					&& mode.state() == PlayState::Paused, "step_unpauses_for_exactly_one_update");
			}

			{
				FakeHost host;
				PlayMode mode(host);
				mode.play();
				host.Calls.clear();
				mode.stop();
				check(host.Calls == Calls({ "stop_scripts", "stop_physics", "cancel_loads", "time_stop", "show_scene(edit)", "restore_document" }) && host.LastRestored == host.Edit && host.Active == host.Edit
					&& mode.state() == PlayState::Edit && mode.play_scene() == nullptr, "stop_order_and_the_edit_scene_comes_back");
			}

			{
				FakeHost host;
				PlayMode mode(host);
				mode.play();
				mode.pause();
				host.Calls.clear();
				mode.stop();
				check(host.Calls == Calls({ "stop_scripts", "stop_physics", "cancel_loads", "time_stop", "show_scene(edit)", "restore_document" }) && mode.state() == PlayState::Edit, "stop_from_paused");
			}

			{
				FakeHost host;
				PlayMode mode(host);
				mode.play();
				Ref<Scene> loaded = CreateRef<Scene>();
				host.Active = loaded; // a script loaded another scene
				host.Calls.clear();
				mode.update();
				mode.update();
				const bool followed = mode.play_scene() == loaded && host.count("start_scripts") == 1 && host.count("stop_scripts") == 1 && host.count("start_physics") == 1;
				mode.stop();
				check(followed && host.LastRestored == host.Edit && host.Active == host.Edit, "scene_replaced_during_play_restarts_scripts_and_stop_restores_the_original");
			}

			{
				FakeHost host;
				PlayMode mode(host);
				mode.play();
				mode.stop();
				host.Calls.clear();
				mode.play();
				check(host.count("show_scene(play)") == 1 && mode.state() == PlayState::Playing, "play_again_after_stop");
			}
		}

		// Hot reload: what the runtime and the instances do when a script changed.
		void check_hot_reload(Check& check)
		{
			{
				Harness h;
				const AssetRef base = h.add_script("hr_base", "return Script:extend{ speak = function() return 1 end }");
				const AssetRef child = h.add_script("hr_child", "local Base = require('assets/scripts/hr_base.lua') return Base:extend{}");
				check(h.Runtime.knows(base) && h.Runtime.knows(child) && !h.Runtime.knows(AssetRef{ AssetGuid::generate(), 0 }), "runtime_knows_the_scripts_it_loaded");
				h.Source.set_source(base, "return Script:extend{ speak = function() return 2 end }");
				std::string error;
				std::vector<AssetRef> reloaded;
				const bool ok = h.Runtime.reload_script(base, error, nullptr, &reloaded);
				const auto has = [&](AssetRef ref) { return std::find(reloaded.begin(), reloaded.end(), ref) != reloaded.end(); };
				check(ok && reloaded.size() == 2 && has(base) && has(child), "reload_reports_what_it_reloaded");
			}

			{
				Harness h;
				const AssetRef ref = h.add_script("hr_fails", R"(return Script:extend{
					on_init = function(self) record('init1') end,
					on_update = function(self, dt) error('boom') end })");
				h.spawn("e", { ref });
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				h.Instances.update(0.016f);
				h.Source.set_source(ref, R"(return Script:extend{
					on_init = function(self) record('init2') end,
					on_update = function(self, dt) record('update2') end })");
				std::string error;
				std::vector<AssetRef> reloaded;
				const bool ok = h.Runtime.reload_script(ref, error, &h.TheScene->registry(), &reloaded);
				h.Instances.script_reloaded(reloaded);
				h.Instances.update(0.016f);
				check(ok && h.Events == std::vector<std::string>({ "init1", "init2", "update2" }), "reload_rebuilds_instances_that_an_error_disabled");
			}

			{
				Harness h;
				const AssetRef healthy = h.add_script("hr_healthy", R"(return Script:extend{
					on_init = function(self) record('healthy init') end,
					on_update = function(self, dt) self.n = (self.n or 0) + 1 record('healthy ' .. self.n) end })");
				h.spawn("e", { healthy });
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				h.Source.set_source(healthy, "return Script:extend{ on_init = function(self) record('REINIT') end, on_update = function(self, dt) self.n = (self.n or 0) + 1 record('healthy ' .. self.n) end }");
				std::string error;
				std::vector<AssetRef> reloaded;
				h.Runtime.reload_script(healthy, error, &h.TheScene->registry(), &reloaded);
				h.Instances.script_reloaded(reloaded);
				h.Instances.update(0.016f);
				check(h.Events == std::vector<std::string>({ "healthy init", "healthy 1", "healthy 2" }), "reload_keeps_instances_that_were_fine");
			}

			{
				Harness h;
				const AssetRef ref = h.Source.add("assets/scripts/hr_late.lua", "return Script:extend{ on_init = function(self) record('ok') end ");
				ScriptClass fake;
				fake.Asset = ref;
				fake.Name = "hr_late";
				register_script_component(fake);
				h.spawn("e", { ref });
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				h.Source.set_source(ref, "return Script:extend{ on_init = function(self) record('ok') end }");
				std::string error;
				std::vector<AssetRef> reloaded;
				const bool ok = h.Runtime.reload_script(ref, error, &h.TheScene->registry(), &reloaded);
				h.Instances.script_reloaded(reloaded);
				h.Instances.update(0.016f);
				check(ok && h.Events == std::vector<std::string>({ "ok" }), "script_that_failed_to_load_is_used_after_a_reload");
			}
		}

		// A bare runtime (the importer has no ScriptInstances) must know the math types, and a property default may be one of them.
		void check_vector_defaults(Check& check)
		{
			MemorySource source;
			const AssetRef ref = source.add("assets/scripts/vector_defaults.lua", R"(return Script:extend{ properties = {
				direction = { type = 'vec3', default = vec3(0, 1, 0) },
				offset = { type = 'vec2', default = vec2(1, 2) },
				tint = { type = 'color', default = vec4(1, 0.5, 0.25, 1) },
				turn = { type = 'quat', default = quat.from_axis_angle(vec3(0, 1, 0), 90) },
				plain = { type = 'vec3', default = { 4, 5, 6 } },
			} })");
			ScriptRuntime runtime(source);
			std::string error;
			const ScriptClass* cls = runtime.load(ref, error);
			check(cls != nullptr && cls->Properties.size() == 5, "vector_defaults_load_in_a_bare_runtime");
			if (cls == nullptr || cls->Properties.size() != 5)
			{
				return;
			}

			const auto find = [&](const char* name) -> const FieldDescriptor&
			{
				return *std::find_if(cls->Properties.begin(), cls->Properties.end(), [&](const FieldDescriptor& field) { return field.Name == name; });
			};
			check(std::get<glm::vec3>(find("direction").Default) == glm::vec3(0.0f, 1.0f, 0.0f), "vec3_default_is_read");
			check(std::get<glm::vec2>(find("offset").Default) == glm::vec2(1.0f, 2.0f), "vec2_default_is_read");
			check(std::get<glm::vec4>(find("tint").Default) == glm::vec4(1.0f, 0.5f, 0.25f, 1.0f), "color_default_is_read");
			check(std::abs(std::get<glm::quat>(find("turn").Default).y - std::sqrt(0.5f)) < 1e-4f, "quat_default_is_read");
			check(std::get<glm::vec3>(find("plain").Default) == glm::vec3(4.0f, 5.0f, 6.0f), "table_default_is_still_read");
		}

		// Findings of the hot reload review.
		void check_hot_reload_review(Check& check)
		{
			{
				const AssetGuid lua = AssetGuid::generate();
				const AssetGuid texture = AssetGuid::generate();
				const std::vector<AssetCatalog::Entry> entries = {
					{ "assets/scripts/mover.lua", lua },
					{ "assets/textures/rock.png", texture },
				};
				check(is_script_asset(entries, lua) && !is_script_asset(entries, texture) && !is_script_asset(entries, AssetGuid::generate()) && !is_script_asset({}, lua),
					"a_lua_asset_in_the_catalog_is_a_script");
			}

			{
				// the importer's thread registers the new component description before the main thread reloads the script: for a frame the
				// registry has no such property any more, and a script that still reads it must not be disabled
				Harness h;
				const AssetRef ref = h.add_script("hr_window", R"(return Script:extend{
					properties = { speed = { type = 'float', default = 5 } },
					on_update = function(self, dt) self.n = (self.n or 0) + 1 record(string.format('%g', self.speed)) self.speed = 7 end })");
				h.spawn("e", { ref });
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);

				ScriptClass newer;
				newer.Asset = ref;
				newer.Name = "hr_window"; // no properties: speed was removed
				register_script_component(newer);
				h.Instances.update(0.016f);
				h.Instances.update(0.016f);
				check(h.Events == std::vector<std::string>({ "5", "5", "5" }), "property_removed_in_the_registry_before_the_reload_does_not_disable_the_instance");
			}
		}
	}

	namespace
	{
		// The globals the sandbox adds to the standard libraries must be exactly the ones editor/engine/lua/wiwa.lua declares.
		void check_stub_globals(Check& check)
		{
			const std::filesystem::path stubPath = "engine/lua/wiwa.lua";
			check(is_lua_stub_file(stubPath), "wiwa_lua_is_a_meta_file");

			const std::set<std::string> parsed = declared_globals(
				"---@meta\nfoo = 1\nlocal bar = 2\nfunction baz(x) end\nfunction qux.m() end\nfunction Q:n() end\n  indented = 3\n--- note = 1\nlocal function f() end\nif a == 2 then end\n");
			check(parsed == std::set<std::string>({ "foo", "baz" }), "declared_globals_reads_assignments_and_plain_functions");

			std::ifstream file(stubPath, std::ios::binary);
			const std::string stub((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			const std::set<std::string> declared = declared_globals(stub);

			MemorySource source;
			ScriptRuntime runtime(source);
			ScriptInstances instances(runtime); // binds the scene, asset, render and engine parts of the API
			sol::state baseline;
			baseline.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string, sol::lib::table, sol::lib::utf8);

			std::set<std::string> added;
			u32 seen = 0;
			for (const auto& entry : runtime.sandbox())
			{
				seen++;
				if (entry.first.get_type() == sol::type::string)
				{
					const std::string name = entry.first.as<std::string>();
					const sol::object inBaseline = baseline[name];
					if (inBaseline.get_type() == sol::type::lua_nil || inBaseline.get_type() == sol::type::none)
					{
						added.insert(name);
					}
				}
			}
			check(seen > 20 && !added.empty(), "sandbox_globals_are_read");

			for (const std::string& name : added)
			{
				if (!declared.contains(name))
				{
					WI_SCRIPT_LOG(Error, "the sandbox has the global '{0}' but wiwa.lua does not declare it", name);
				}
			}
			for (const std::string& name : declared)
			{
				if (!added.contains(name))
				{
					WI_SCRIPT_LOG(Error, "wiwa.lua declares the global '{0}' but the sandbox does not have it", name);
				}
			}
			check(added == declared, "stub_declares_exactly_the_sandbox_globals");
		}

		ComponentInfo stub_component(const std::string& name, const std::string& displayName, std::vector<FieldDescriptor> fields)
		{
			ComponentInfo info;
			info.Name = name;
			info.DisplayName = displayName;
			info.Fields = std::move(fields);
			return info;
		}

		bool loads_as_lua(const std::string& text)
		{
			sol::state lua;
			return lua.load(text).valid();
		}

		void check_component_stubs(Check& check)
		{
			const auto field = [](const char* name, FieldType type) { FieldDescriptor f; f.Name = name; f.Type = type; return f; };

			const ComponentInfo light = stub_component("Light", "", { field("Intensity", FieldType::Float), field("Color", FieldType::Color) });
			const ComponentInfo enemy = stub_component("script:abcd", "Enemy", { field("health", FieldType::Int), field("target", FieldType::Entity), field("icons", FieldType::AssetList) });
			const std::string text = generate_component_stubs({ &enemy, &light });

			check(contains(text, "---@class Comp.Light"), "stubs_have_a_class_per_component");
			check(contains(text, "---@field Intensity number") && contains(text, "---@field Color vec4"), "stubs_map_float_and_color_fields");
			check(contains(text, "---@class Comp.Enemy") && contains(text, "---@field health integer"), "stubs_use_the_script_file_name_and_int_is_integer");
			check(contains(text, "---@field target Entity?") && contains(text, "---@field icons Asset[]"), "stubs_map_entity_and_asset_list_fields");
			check(contains(text, "name: \"Enemy\"): Comp.Enemy") && contains(text, "name: \"Light\"): Comp.Light"), "stubs_overload_get_by_component_name");
			check(contains(text, "---@alias ComponentName \"Enemy\"|\"Light\""), "stubs_list_the_component_names_sorted");
			check(loads_as_lua(text), "stubs_are_valid_lua");
			check(text == generate_component_stubs({ &light, &enemy }), "stubs_do_not_depend_on_the_order_of_the_input");

			const std::string empty = generate_component_stubs({});
			check(loads_as_lua(empty) && !contains(empty, "@overload") && contains(empty, "function Entity:get"), "stubs_of_no_components_are_valid_and_still_declare_get");

			const ComponentInfo odd = stub_component("script:odd", "my \"x\"-script", { field("hp", FieldType::Int) });
			const std::string oddText = generate_component_stubs({ &odd });
			check(loads_as_lua(oddText) && contains(oddText, "---@class Comp.my__x__script") && contains(oddText, "name: \"my \\\"x\\\"-script\""), "stubs_sanitize_and_escape_odd_script_names");

			const ComponentInfo twin = stub_component("script:twin", "Enemy", { field("other", FieldType::Bool) });
			const std::string twinText = generate_component_stubs({ &enemy, &twin });
			check(loads_as_lua(twinText) && contains(twinText, "-- duplicate: Enemy") && !contains(twinText, "---@field other"), "stubs_skip_a_second_component_with_the_same_name");
		}

		// The LuaLS support files: stubs the importer must leave alone, the stub the editor writes, and the project settings.
		void check_lua_stubs(Check& check, AssetPipelineModule& pipeline)
		{
			const std::filesystem::path dir = std::filesystem::temp_directory_path() / "wiwa_lua_stubs_smoke";
			std::filesystem::remove_all(dir);
			std::filesystem::create_directories(dir);

			const auto write = [](const std::filesystem::path& path, const std::string& text)
			{
				std::ofstream(path, std::ios::binary) << text;
			};
			const auto read = [](const std::filesystem::path& path)
			{
				std::ifstream file(path, std::ios::binary);
				return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			};

			// write_if_changed: written once, left alone while the text is the same, written again when it changes
			const std::filesystem::path out = dir / "lua" / "out.lua";
			check(write_if_changed(out, "one") && read(out) == "one", "write_if_changed_writes_a_new_file");
			const auto stamp = std::filesystem::last_write_time(out);
			std::this_thread::sleep_for(std::chrono::milliseconds(30));
			check(!write_if_changed(out, "one") && std::filesystem::last_write_time(out) == stamp, "write_if_changed_leaves_identical_text_alone");
			check(write_if_changed(out, "two") && read(out) == "two", "write_if_changed_writes_changed_text");
			check(!std::filesystem::exists(out.string() + ".tmp"), "write_if_changed_leaves_no_temp_file");
			write(dir / "plain_file", "x");
			check(!write_if_changed(dir / "plain_file" / "nested.lua", "x"), "write_failure_is_not_fatal");

			// the registered components are written; several threads asking at once leave one whole file
			ComponentRegistry::get().register_component(stub_component("script:lua_stub_smoke", "LuaStubSmoke", { FieldDescriptor{ "hp", FieldType::Int } }));
			const std::filesystem::path generated = dir / "generated.lua";
			check(write_component_stubs(generated) && contains(read(generated), "---@class Comp.LuaStubSmoke"), "registered_components_are_written");
			std::vector<std::thread> threads;
			for (int i = 0; i < 8; i++)
			{
				threads.emplace_back([&]() { write_component_stubs(generated); });
			}
			for (std::thread& thread : threads)
			{
				thread.join();
			}
			const std::string finalText = read(generated);
			check(loads_as_lua(finalText) && contains(finalText, "---@class Comp.LuaStubSmoke") && !std::filesystem::exists(generated.string() + ".tmp"), "refresh_is_thread_safe");
			ComponentRegistry::get().unregister_component("script:lua_stub_smoke");

			// .luarc.json: created, then left alone; the editor's own file is refreshed when it is out of date; a file without the marker is the user's
			const std::filesystem::path luarc = dir / "luarc" / ".luarc.json";
			check(ensure_luarc(luarc) == LuarcResult::Created, "luarc_is_created");
			const std::string luarcText = read(luarc);
			check(luarcText.starts_with("// generated by Wiwa; remove this line to take over the file\n") && contains(luarcText, "\"library/lua\"") && contains(luarcText, "\"engine/lua\"") && contains(luarcText, "Lua 5.4"), "luarc_points_at_both_stubs");
			check(contains(luarcText, "\"os\": \"disable\"") && contains(luarcText, "\"io\": \"disable\"") && contains(luarcText, "\"debug\": \"disable\"") && contains(luarcText, "\"package\": \"disable\""), "luarc_disables_the_libraries_the_sandbox_lacks");
			check(ensure_luarc(luarc) == LuarcResult::LeftAlone && read(luarc) == luarcText, "current_luarc_is_left_alone");
			write(luarc, luarc_text() + " ");
			check(ensure_luarc(luarc) == LuarcResult::Refreshed && read(luarc) == luarc_text(), "edited_luarc_with_the_marker_is_refreshed");
			write(luarc, "{ \"runtime.version\": \"LuaJIT\" }\n");
			check(ensure_luarc(luarc) == LuarcResult::LeftAlone && read(luarc) == "{ \"runtime.version\": \"LuaJIT\" }\n", "luarc_without_the_marker_is_the_users");
			check(ensure_luarc(dir / "plain_file" / ".luarc.json") == LuarcResult::Failed, "luarc_write_failure_is_not_fatal");

			// the editor itself keeps library/lua/generated.lua current: written at start, rewritten when a component is registered or removed
			const auto wait_until = [](const std::function<bool()>& condition)
			{
				for (int i = 0; i < 60 && !condition(); i++)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
				}
				return condition();
			};
			const std::filesystem::path live = "library/lua/generated.lua";
			check(wait_until([&]() { return std::filesystem::exists(live); }), "editor_writes_the_generated_stub_at_start");
			ComponentRegistry::get().register_component(stub_component("script:lua_stub_live", "LuaStubLive", { FieldDescriptor{ "hp", FieldType::Int } }));
			check(wait_until([&]() { return contains(read(live), "Comp.LuaStubLive"); }), "generated_stub_follows_a_registered_component");
			ComponentRegistry::get().unregister_component("script:lua_stub_live");
			check(wait_until([&]() { return !contains(read(live), "Comp.LuaStubLive"); }), "generated_stub_follows_a_removed_component");

			const std::filesystem::path meta = dir / "meta.lua";
			const std::filesystem::path plain = dir / "plain.lua";
			write(meta, "---@meta\nfoo = 1\n");
			write(plain, "return {}\n");
			check(is_lua_stub_file(meta), "stub_file_detected");
			check(!is_lua_stub_file(plain), "plain_lua_is_not_a_stub");
			check(!is_lua_stub_file(dir / "missing.lua"), "missing_file_is_not_a_stub");

			// a stub inside a source root is not imported: no asset, no .meta sidecar
			AssetDatabase& database = pipeline.get_database();
			const std::filesystem::path inRoot = database.get_roots()[1].Source / "lua_stubs_smoke_stub.lua";
			write(inRoot, "---@meta\nbar = 2\n");
			database.reimport(inRoot);
			database.scan();
			const bool hasMeta = std::filesystem::exists(inRoot.string() + ".meta");
			std::filesystem::remove(inRoot);
			std::filesystem::remove(inRoot.string() + ".meta");
			check(!hasMeta, "stub_file_gets_no_meta");

			std::filesystem::remove_all(dir);
		}

		bool near_rect(const UiRect& rect, glm::vec2 min, glm::vec2 max, float tolerance = 0.5f)
		{
			return glm::length(rect.Min - min) <= tolerance && glm::length(rect.Max - max) <= tolerance;
		}

		// ---- the UI API ------------------------------------------------------------------------------------------------------------

		UiWidget ui_widget(UiId id, UiId parent, WidgetKind kind, const char* name, glm::vec2 size)
		{
			UiWidget widget;
			widget.Id = id;
			widget.Parent = parent;
			widget.Kind = kind;
			widget.Name = name;
			widget.OffsetMax = size;
			return widget;
		}

		// A scene with a camera, a "Hud" entity that shows a panel (Menu > Start > Label, Volume, Health, Mute, Marker) and the scripts of a test.
		struct UiHarness
		{
			Harness H;
			UiModule Module{};
			std::unordered_map<AssetGuid, Ref<AssetSlot>> Slots{};
			AssetRef Panel{};
			entt::entity Hud = entt::null;
			entt::entity Target = entt::null;
			glm::vec2 Canvas{ 1920.0f, 1080.0f };

			UiHarness()
			{
				const entt::entity camera = H.TheScene->create("Camera");
				H.TheScene->registry().get<Transform>(camera).Position = { 0.0f, 0.0f, 5.0f };
				H.TheScene->registry().emplace<Camera>(camera).Primary = true;
				Target = H.TheScene->create("Target");

				UiPanelData data;
				UiWidget menu = ui_widget(1, 0, WidgetKind::VStack, "Menu", { 300.0f, 300.0f });
				UiWidget start = ui_widget(2, 1, WidgetKind::Button, "Start", { 100.0f, 30.0f });
				UiWidget label = ui_widget(3, 2, WidgetKind::Text, "Label", { 0.0f, 0.0f });
				label.AnchorMax = { 1.0f, 1.0f };
				label.Text = "Start";
				UiWidget volume = ui_widget(4, 1, WidgetKind::Slider, "Volume", { 200.0f, 20.0f });
				UiWidget health = ui_widget(5, 1, WidgetKind::Bar, "Health", { 100.0f, 30.0f });
				UiWidget mute = ui_widget(6, 1, WidgetKind::Toggle, "Mute", { 30.0f, 30.0f });
				UiWidget marker = ui_widget(7, 0, WidgetKind::Panel, "Marker", { 40.0f, 40.0f });
				marker.Color = { 1.0f, 0.0f, 0.0f, 1.0f };
				marker.AnchorMin = marker.AnchorMax = { 1.0f, 1.0f };
				marker.OffsetMin = { -40.0f, -40.0f };
				marker.OffsetMax = { 0.0f, 0.0f };
				data.Widgets = { menu, start, label, volume, health, mute, marker };
				data.NextId = 8;

				Panel = AssetRef{ AssetGuid::generate(), 0 };
				auto slot = std::make_shared<AssetSlot>();
				slot->Data = std::make_shared<UiPanelData>(data);
				slot->State = LoadState::Ready;
				Slots[Panel.Guid] = slot;

				Module.set_panel_loader([this](const AssetRef& ref) -> Handle<UiPanelData>
				{
					const auto it = Slots.find(ref.Guid);
					return it == Slots.end() ? Handle<UiPanelData>{} : Handle<UiPanelData>(ref, it->second);
				});
				Module.set_input_source(&Module.scripted_input());
				H.Instances.context().Ui = &Module;

				Hud = H.TheScene->create("Hud");
				H.TheScene->registry().emplace<UICanvas>(Hud, UICanvas{ Panel, 0, true });
			}

			// Adds a script and an entity that has it.
			entt::entity spawn_script(const std::string& name, const std::string& source)
			{
				return H.spawn(name + "_entity", { H.add_script(name, source) });
			}

			void script(const std::string& name, const std::string& source) { spawn_script(name, source); }

			// One frame as the engine runs it: the UI looks at the input, then the scripts run.
			void frame()
			{
				H.TheScene->update_world_transforms();
				Module.update_interaction(*H.TheScene, Canvas);
				H.Instances.update(0.016f);
			}

			UiDrawList draw()
			{
				H.TheScene->update_world_transforms();
				UiDrawList list;
				Module.build_draw_list(*H.TheScene, Canvas, list);
				return list;
			}

			UiPanelInstance& instance() { return *Module.instance_of(Hud); }
			void click(float x, float y)
			{
				Module.scripted_input().move_to({ x, y });
				Module.scripted_input().press(0);
				Module.scripted_input().release(0);
			}
		};

		void check_ui(Check& check)
		{
			{
				UiHarness ui;
				ui.script("finder", R"(return Script:extend{ on_init = function(self)
					local panel = UI.panel(find_entity('Hud'))
					local label = panel:find('Menu/Start/Label')
					record(label.text)
					label.text = 'Go'
					record(label.text)
					record(tostring(panel:find('Menu/Nope')))
					record(tostring(panel:find('')))
					record(tostring(UI.panel(find_entity('Camera'))))
					record(tostring(panel.ready))
				end })");
				ui.H.Instances.start(ui.H.TheScene);
				ui.frame();
				check(ui.H.Events == std::vector<std::string>({ "Start", "Go", "nil", "nil", "nil", "true" }), "ui_lua_find_and_set_text");
				check(ui.instance().data().Widgets[2].Text == "Go", "ui_lua_find_and_set_text: the instance changed");
				const UiPanelData* asset = static_cast<const UiPanelData*>(ui.Slots[ui.Panel.Guid]->Data.get());
				check(asset->Widgets[2].Text == "Start", "ui_lua_find_and_set_text: the asset did not");
			}

			{
				UiHarness ui;
				ui.script("values", R"(return Script:extend{ on_init = function(self)
					local panel = UI.panel(find_entity('Hud'))
					local volume = panel:find('Menu/Volume')
					volume.value = 0.7
					record(string.format('%.2f', volume.value))
					record(string.format('%.1f %.1f', volume.min, volume.max))
					local health = panel:find('Menu/Health')
					health.value = 0.5
					health.visible = true
					health.enabled = false
					health.color = vec4(0, 1, 0, 1)
					health.opacity = 0.5
					record(tostring(health.enabled) .. ' ' .. string.format('%.1f %.1f', health.color.y, health.opacity))
				end })");
				ui.H.Instances.start(ui.H.TheScene);
				ui.frame();
				check(ui.H.Events == std::vector<std::string>({ "0.70", "0.0 1.0", "false 1.0 0.5" }), "ui_lua_value_roundtrip");
				const UiDrawList list = ui.draw();
				// the bar: a background quad and a fill quad of half the width, after the 4 quads of the button... find the green one
				bool halfGreen = false;
				for (size_t quad = 0; quad * 4 < list.Vertices.size(); quad++)
				{
					if (list.Vertices[quad * 4].Color == ui_pack_color({ 0.0f, 1.0f, 0.0f, 0.5f }))
					{
						halfGreen = std::abs((list.Vertices[quad * 4 + 1].Pos.x - list.Vertices[quad * 4].Pos.x) - 50.0f) < 0.5f;
					}
				}
				check(halfGreen, "ui_lua_value_roundtrip: the bar is drawn half full in the colour and opacity the script set");
			}

			{
				UiHarness ui;
				ui.script("clicker", R"(return Script:extend{
					on_init = function(self)
						local start = UI.panel(find_entity('Hud')):find('Menu/Start')
						start:on('click', function(me) record('clicked') end)
					end,
					on_update = function(self, dt) record('update') end })");
				ui.H.Instances.start(ui.H.TheScene);
				ui.frame();
				ui.H.Events.clear();
				ui.click(50.0f, 15.0f);
				ui.frame();
				check(ui.H.Events == std::vector<std::string>({ "clicked", "update" }), "ui_lua_click_handler: a click runs the handler once, before that frame's on_update");
				ui.frame();
				check(ui.H.count("clicked") == 1, "ui_lua_click_handler: and not again");
			}

			{
				UiHarness ui;
				ui.script("slider", R"(return Script:extend{ on_init = function(self)
					local volume = UI.panel(find_entity('Hud')):find('Menu/Volume')
					volume:on('value_changed', function(me, value) record(string.format('%.2f', value)) end)
					volume:on('hover_enter', function(me) record('enter') end)
					volume:on('focus', function(me) record('focus') end)
				end })");
				ui.H.Instances.start(ui.H.TheScene);
				ui.frame();
				ui.Module.scripted_input().move_to({ 50.0f, 40.0f });
				ui.Module.scripted_input().press(0);
				ui.frame();
				check(ui.H.Events == std::vector<std::string>({ "enter", "focus", "0.25" }), "ui_lua_value_changed_arg: the handlers get their events, and value_changed the new value");
			}

			{
				UiHarness ui;
				const entt::entity ownerEntity = ui.spawn_script("owner", R"(return Script:extend{ on_init = function(self)
					UI.panel(find_entity('Hud')):find('Menu/Start'):on('click', function(me) record('owner') end)
				end })");
				ui.H.Instances.start(ui.H.TheScene);
				ui.frame();
				ui.click(50.0f, 15.0f);
				ui.frame();
				check(ui.H.count("owner") == 1, "ui_lua_handler_cleared_on_destroy: the handler works while its script lives");

				ui.H.TheScene->destroy(ownerEntity);
				ui.frame(); // the script is reaped
				ui.click(50.0f, 15.0f);
				ui.frame();
				check(ui.H.count("owner") == 1, "ui_lua_handler_cleared_on_destroy: once its entity is gone the handler is gone, and nothing crashes");
			}

			{
				UiHarness ui;
				ui.script("restart", R"(return Script:extend{ on_init = function(self)
					UI.panel(find_entity('Hud')):find('Menu/Start'):on('click', function(me) record('restart') end)
				end })");
				ui.H.Instances.start(ui.H.TheScene);
				ui.frame();
				ui.H.Instances.stop();
				ui.click(50.0f, 15.0f);
				ui.frame();
				ui.H.Instances.start(ui.H.TheScene);
				ui.frame();
				ui.click(50.0f, 15.0f);
				ui.frame();
				check(ui.H.count("restart") == 1, "ui_lua_handler_cleared_on_stop_restart: after a stop and a start the handler is registered once, not twice");
			}

			{
				UiHarness ui;
				ui.script("stale", R"(return Script:extend{
					on_init = function(self)
						self.panel = UI.panel(find_entity('Hud'))
						self.label = self.panel:find('Menu/Start/Label')
						self.frames = 0
					end,
					on_update = function(self, dt)
						self.frames = self.frames + 1
						if self.frames == 1 then
							kill(find_entity('Hud'))
						elseif self.frames == 2 then
							record(tostring(self.label.text))
							self.label.text = 'x'
							self.label:on('click', function() end)
							self.label:off('click')
							self.label:clear_world_anchor()
							record(tostring(self.panel.visible))
							record(tostring(self.panel:find('Menu')))
							record('ok')
						end
					end })");
				ui.H.Instances.start(ui.H.TheScene);
				ui.frame();
				ui.frame(); // the canvas is gone
				check(ui.H.Events == std::vector<std::string>({ "nil", "nil", "nil", "ok" }), "ui_lua_stale_handle_safe: handles to a widget whose canvas is gone read nothing and change nothing, without an error");
			}

			{
				UiHarness ui;
				ui.script("typed", R"(return Script:extend{ on_init = function(self)
					local label = UI.panel(find_entity('Hud')):find('Menu/Start/Label')
					local ok, err = pcall(function() label.text = 5 end)
					record(tostring(ok))
					record(tostring(string.find(err, 'text', 1, true) ~= nil))
					local ok2, err2 = pcall(function() label.nonsense = 1 end)
					record(tostring(ok2))
					record(tostring((pcall(function() return label.nonsense end))))
					local start = UI.panel(find_entity('Hud')):find('Menu/Start')
					local ok3 = pcall(function() start:on('nope', function() end) end)
					record(tostring(ok3))
					local ok4 = pcall(function() start:on('click', 5) end)
					record(tostring(ok4))
				end })");
				ui.H.Instances.start(ui.H.TheScene);
				ui.frame();
				check(ui.H.Events == std::vector<std::string>({ "false", "true", "false", "false", "false", "false" }), "ui_lua_wrong_type_error: a bad value, an unknown property (read or written) or event, and a handler that is not a function are Lua errors");
			}

			{
				UiHarness ui;
				ui.script("anchor", R"(return Script:extend{
					on_init = function(self)
						self.marker = UI.panel(find_entity('Hud')):find('Marker')
					end,
					on_update = function(self, dt)
						self.frames = (self.frames or 0) + 1
						if self.frames == 2 then
							self.marker.world_anchor = find_entity('Target')
							record(tostring(self.marker.world_anchor.name))
						elseif self.frames == 4 then
							self.marker.world_offset = vec3(0, 1, 0)
							record(string.format('%.1f', self.marker.world_offset.y))
						elseif self.frames == 6 then
							self.marker:clear_world_anchor()
							record(tostring(self.marker.world_anchor))
						end
					end })");
				ui.H.Instances.start(ui.H.TheScene);
				ui.frame();
				const UiDrawList before = ui.draw();
				ui.frame();
				const UiDrawList anchored = ui.draw();
				ui.frame(); // 3
				ui.frame(); // 4: the offset is set
				const UiDrawList raised = ui.draw();
				ui.frame(); // 5
				ui.frame(); // 6: the anchor is cleared
				const UiDrawList cleared = ui.draw();
				auto marker_rect = [](const UiDrawList& list) -> std::optional<UiRect>
				{
					for (size_t quad = 0; quad * 4 < list.Vertices.size(); quad++)
					{
						if (list.Vertices[quad * 4].Color == ui_pack_color({ 1.0f, 0.0f, 0.0f, 1.0f }))
						{
							return UiRect{ list.Vertices[quad * 4].Pos, list.Vertices[quad * 4 + 2].Pos };
						}
					}
					return std::nullopt;
				};
				const std::optional<UiRect> a = marker_rect(before);
				const std::optional<UiRect> b = marker_rect(anchored);
				const std::optional<UiRect> c = marker_rect(raised);
				const std::optional<UiRect> d = marker_rect(cleared);
				check(a && near_rect(*a, { 1880, 1040 }, { 1920, 1080 }), "ui_lua_world_anchor_set: before, the marker is in the corner");
				check(b && near_rect(*b, { 940, 520 }, { 980, 560 }, 1.0f), "ui_lua_world_anchor_set: after world_anchor = entity it is where the entity is on the screen");
				check(c && c->Min.y < b->Min.y - 20.0f, "ui_lua_world_anchor_set: world_offset moves it");
				check(d && near_rect(*d, { 1880, 1040 }, { 1920, 1080 }), "ui_lua_clear_world_anchor: clear_world_anchor puts it back");
				check(ui.H.Events == std::vector<std::string>({ "Target", "1.0", "nil" }), "ui_lua_world_anchor_set: the properties read back");
			}

			{
				UiHarness ui;
				ui.script("input", R"(return Script:extend{
					on_init = function(self)
						UI.panel(find_entity('Hud')):find('Menu/Start'):on('click', function(me) record('click') end)
					end,
					on_update = function(self, dt)
						if self.sent then return end
						self.sent = true
						UI.input.move_to(50, 15)
						UI.input.press(0)
						UI.input.release(0)
					end })");
				ui.H.Instances.start(ui.H.TheScene);
				ui.frame();
				ui.frame();
				check(ui.H.count("click") == 1, "ui_lua_input: input made in Lua reaches the widgets in the next frame");

				UiHarness other;
				other.script("input2", R"(return Script:extend{ on_init = function(self)
					UI.input.nav('down')
					UI.input.nav('submit')
					UI.input.scroll(1)
					UI.input.clear_pointer()
					local ok = pcall(function() UI.input.nav('sideways') end)
					record(tostring(ok))
				end })");
				other.H.Instances.start(other.H.TheScene);
				other.frame();
				check(other.H.Events == std::vector<std::string>({ "false" }), "ui_lua_input: navigation, scroll and clear_pointer exist, and an unknown navigation is an error");
			}

			{
				UiHarness ui;
				ui.script("visibility", R"(return Script:extend{ on_init = function(self)
					local panel = UI.panel(find_entity('Hud'))
					panel.visible = false
					record(tostring(panel.visible))
				end })");
				ui.H.Instances.start(ui.H.TheScene);
				ui.frame();
				check(ui.H.Events == std::vector<std::string>({ "false" }) && ui.draw().empty(), "ui_lua_panel_visible: a hidden panel draws nothing");
			}

			{
				UiHarness ui;
				ui.script("off", R"(return Script:extend{ on_init = function(self)
					local start = UI.panel(find_entity('Hud')):find('Menu/Start')
					local function once() record('once') end
					start:on('click', once)
					start:on('click', function() record('stays') end)
					start:off('click', once)
				end })");
				ui.H.Instances.start(ui.H.TheScene);
				ui.frame();
				ui.click(50.0f, 15.0f);
				ui.frame();
				check(ui.H.count("once") == 0 && ui.H.count("stays") == 1, "ui_lua_off: off(event, fn) removes that handler only");
			}

			{
				UiHarness ui;
				ui.script("thrower", R"(return Script:extend{
					on_init = function(self)
						local start = UI.panel(find_entity('Hud')):find('Menu/Start')
						start:on('click', function() error('boom') end)
						start:on('click', function() record('second') end)
					end,
					on_update = function(self, dt) record('update') end })");
				ui.H.Instances.start(ui.H.TheScene);
				ui.frame();
				ui.H.Events.clear();
				WI_SCRIPT_LOG(Info, "Smoke test: the next script error is expected");
				ui.click(50.0f, 15.0f);
				ui.frame();
				check(ui.H.count("second") == 1 && ui.H.count("update") == 1, "ui_lua_handler_error: a handler that raises an error does not stop the other handlers or the script's hooks");
			}

			{
				Harness h;
				h.spawn("e", { h.add_script("nomodule", R"(return Script:extend{ on_init = function(self)
					local ok = pcall(function() return UI.panel(self.entity) end)
					record(tostring(ok))
				end })") });
				h.Instances.start(h.TheScene);
				h.Instances.update(0.016f);
				check(h.Events == std::vector<std::string>({ "false" }), "ui_lua_no_module: without a UI module the calls are Lua errors");
			}
		}

	}

	namespace
	{
		// Check 6: Lua and physics. A script on a ball that falls onto the ground gets on_collision_enter with the ground as `other` and a
		// normal that pushes the ball up; physics.raycast from a script returns the entity it hit; the ball's script also has on_physics_update.
		// A script on another entity whose on_physics_update raises an error stops, and the ball's script goes on.
		void check_physics_scripts(Check& check)
		{
			Harness h;
			PhysicsModule physics;
			physics.set_auto_start(false);
			h.Instances.context().Physics = &physics;

			const entt::entity ground = h.TheScene->create("Ground");
			Collider groundCollider;
			groundCollider.Size = glm::vec3(20.0f, 0.5f, 20.0f);
			h.TheScene->registry().emplace<Collider>(ground, groundCollider);

			const AssetRef ballScript = h.add_script("ball", R"(return Script:extend{
				on_collision_enter = function(self, other, contact)
					record('enter:' .. other.name .. ':' .. tostring(contact.normal.y > 0.9) .. ':' .. tostring(contact.impulse > 0) .. ':' .. tostring(contact.point.y > 0.4 and contact.point.y < 0.6))
				end,
				on_physics_update = function(self, dt)
					record('tick')
					if not self.rayDone then
						self.rayDone = true
						local hit = physics.raycast(vec3(20, 10, 0), vec3(0, -1, 0), 100)
						record('ray:' .. (hit and hit.entity.name or 'nil'))
						local none = physics.raycast(vec3(20, 10, 0), vec3(0, 1, 0), 100)
						record('rayup:' .. tostring(none))
						local overlap = physics.overlap_sphere(vec3(0, 0.5, 0), 0.4)
						record('overlap:' .. #overlap)
					end
				end })");
			const AssetRef badScript = h.add_script("bad", R"(return Script:extend{
				on_physics_update = function(self, dt) record('bad'); error('boom') end })");

			const entt::entity ball = h.spawn("Ball", { ballScript });
			h.TheScene->registry().get<Transform>(ball).Position = glm::vec3(0.0f, 3.0f, 0.0f);
			h.TheScene->registry().emplace<RigidBody>(ball, RigidBody{ MotionType::Dynamic });
			Collider ballCollider;
			ballCollider.Shape = ColliderShape::Sphere;
			ballCollider.Size = glm::vec3(0.5f);
			h.TheScene->registry().emplace<Collider>(ball, ballCollider);
			h.spawn("Bad", { badScript });

			check(physics.start_world(h.TheScene), "physics_scripts_world: the world starts");
			physics.thread().set_paused(false); // the editor's game clock is not playing in this check
			h.Instances.start(h.TheScene);
			for (int frame = 0; frame < 400 && h.count("tick") < 90; frame++)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(16));
				physics.pump();
				h.Instances.dispatch_physics(physics);
				h.Instances.update(0.016f);
			}
			h.Instances.stop();
			physics.stop_world();

			check(h.count("enter:Ground:true:true:true") == 1, "physics_scripts_enter: on_collision_enter(self, other, contact) arrives once with the ground, an upward normal, an impulse and the point on the ground, got " + [&]() { std::string all; for (const std::string& e : h.Events) { if (e.rfind("enter", 0) == 0) { all += e + " "; } } return all; }());
			check(h.count("ray:Ground") == 1, "physics_scripts_raycast: physics.raycast returns the entity it hit");
			check(h.count("rayup:nil") == 1, "physics_scripts_raycast_miss: a ray that hits nothing gives nil");
			check(h.count("overlap:1") == 1 || h.count("overlap:0") == 1, "physics_scripts_overlap: physics.overlap_sphere returns a table");
			check(h.count("tick") >= 20, "physics_scripts_update: on_physics_update runs every step, got " + std::to_string(h.count("tick")));
			check(h.count("bad") == 1, "physics_scripts_error_isolated: a script that raises in on_physics_update runs once and is disabled, got " + std::to_string(h.count("bad")));
		}

		// Check 6b: Lua and joints. joint.set_motor drives a hinge (and writes the Joint component), joint.set_limits writes the limits, a
		// joint that breaks calls on_joint_break(self, other) on the entity that owns it (other is nil for a joint to the world), and a bad
		// call is a Lua error.
		void check_joint_scripts(Check& check)
		{
			Harness h;
			PhysicsModule physics;
			physics.set_auto_start(false);
			h.Instances.context().Physics = &physics;

			const auto anchor_at = [&](const char* name, glm::vec3 position)
			{
				const entt::entity anchor = h.TheScene->create(name);
				h.TheScene->registry().get<Transform>(anchor).Position = position;
				Collider collider;
				collider.Size = glm::vec3(0.1f);
				h.TheScene->registry().emplace<Collider>(anchor, collider);
				return anchor;
			};

			// a motor-driven door
			const entt::entity doorAnchor = anchor_at("DoorAnchor", glm::vec3(0.0f, 5.5f, 0.0f));
			const AssetRef doorScript = h.add_script("door", R"(return Script:extend{
				on_physics_update = function(self, dt)
					self.ticks = (self.ticks or 0) + 1
					if self.ticks == 1 then
						joint.set_motor(self.entity, 'velocity', 1.0, 1000000)
						joint.set_limits(self.entity, -80, 80)
						record('bad_mode:' .. tostring(pcall(joint.set_motor, self.entity, 'bogus', 1, 1)))
						record('bad_limits:' .. tostring(pcall(joint.set_limits, self.entity, 'x', 1)))
					end
					local w = self.entity:body():get_angular_velocity()
					if not self.reached and self.ticks >= 10 and self.ticks <= 70 and w ~= nil and math.abs(w.z - 1.0) < 0.1 then
						self.reached = true
						record('w:true')
					end
					if self.ticks == 100 and not self.reached then record('w:false') end
				end })");
			const entt::entity door = h.spawn("Door", { doorScript });
			h.TheScene->registry().get<Transform>(door).Position = glm::vec3(1.0f, 5.0f, 0.0f);
			h.TheScene->registry().emplace<RigidBody>(door, RigidBody{ MotionType::Dynamic });
			Collider plank;
			plank.Size = glm::vec3(1.0f, 0.1f, 0.1f);
			h.TheScene->registry().emplace<Collider>(door, plank);
			Joint hinge;
			hinge.Type = JointType::Hinge;
			hinge.Connected = h.TheScene->registry().get<Id>(doorAnchor).Guid;
			hinge.AnchorA = glm::vec3(-1.0f, 0.0f, 0.0f);
			hinge.AnchorB = glm::vec3(0.0f, -0.5f, 0.0f);
			hinge.Axis = glm::vec3(0.0f, 0.0f, 1.0f);
			h.TheScene->registry().emplace<Joint>(door, hinge);

			// an entity without a Joint: joint.* is a Lua error
			const AssetRef plainScript = h.add_script("plain", R"(return Script:extend{
				on_physics_update = function(self, dt)
					if not self.done then
						self.done = true
						record('bad_entity:' .. tostring(pcall(joint.set_motor, self.entity, 'velocity', 1, 1)))
					end
				end })");
			h.spawn("Plain", { plainScript });

			// two boxes that hang by a fixed joint that is too weak: one from the world, one from a static body
			const AssetRef breakScript = h.add_script("breaker", R"(return Script:extend{
				on_joint_break = function(self, other)
					record('break:' .. self.name .. ':' .. (other and other.name or 'nil'))
				end })");
			const auto hanger = [&](const char* name, float x, const entt::entity connected)
			{
				const entt::entity box = h.spawn(name, { breakScript });
				h.TheScene->registry().get<Transform>(box).Position = glm::vec3(x, 4.7f, 0.0f);
				h.TheScene->registry().emplace<RigidBody>(box, RigidBody{ MotionType::Dynamic });
				Collider collider;
				collider.Size = glm::vec3(0.5f);
				h.TheScene->registry().emplace<Collider>(box, collider);
				Joint joint;
				joint.Type = JointType::Fixed;
				joint.AnchorA = glm::vec3(0.0f, 0.5f, 0.0f);
				joint.AnchorB = connected == entt::null ? glm::vec3(x, 5.2f, 0.0f) : glm::vec3(0.0f, -0.8f, 0.0f);
				joint.Connected = connected == entt::null ? EntityGuid{} : h.TheScene->registry().get<Id>(connected).Guid;
				joint.BreakForce = 5000.0f;
				h.TheScene->registry().emplace<Joint>(box, joint);
			};
			hanger("HangWorld", 10.0f, entt::null);
			hanger("HangAnchor", 20.0f, anchor_at("Ceiling", glm::vec3(20.0f, 6.0f, 0.0f)));

			check(physics.start_world(h.TheScene), "joint_scripts_world: the world starts");
			physics.thread().set_paused(false); // the editor's game clock is not playing in this check
			h.Instances.start(h.TheScene);
			for (int frame = 0; frame < 600 && !(h.count("w:true") + h.count("w:false") > 0 && h.count("break:HangWorld:nil") > 0 && h.count("break:HangAnchor:Ceiling") > 0); frame++)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(16));
				physics.pump();
				h.Instances.dispatch_physics(physics);
				h.Instances.update(0.016f);
				physics.pre_render_update(0.016f); // sends the Joint changes the scripts made, as the engine does every frame
			}
			h.Instances.stop();
			physics.stop_world();

			check(h.count("w:true") == 1, "joint_scripts_motor: joint.set_motor turns the door at 1 rad/s");
			check(h.count("bad_entity:false") == 1 && h.count("bad_mode:false") == 1 && h.count("bad_limits:false") == 1,
				"joint_scripts_errors: an entity without a Joint, an unknown mode and a bad limit are Lua errors");
			const Joint& after = h.TheScene->registry().get<Joint>(door);
			check(after.MotorMode == JointMotorMode::Velocity && after.MotorTarget == 1.0f && after.LimitMin == -80.0f && after.LimitMax == 80.0f,
				"joint_scripts_component: the calls are written to the Joint component");
			check(h.count("break:HangWorld:nil") == 1, "joint_scripts_break_world: on_joint_break(self, nil) arrives once for a joint to the world");
			check(h.count("break:HangAnchor:Ceiling") == 1, "joint_scripts_break_other: on_joint_break(self, other) names the entity the joint was connected to");
		}
	}

	bool run_script_smoke_test(AssetPipelineModule& pipeline)
	{
		Check check;

		check_script_data(check);
		check_runtime_classes(check);
		check_sandbox(check);
		check_budget(check);
		check_require(check);
		check_require_scan(check);
		check_importer(check);
		check_scene_scripts(check);
		check_lifecycle(check);
		check_api(check);
		check_engine_bindings(check, pipeline);
		check_scene_changes_and_reload(check);
		check_pak(check);
		check_review_fixes(check);
		check_time(check);
		check_hot_reload(check);
		check_vector_defaults(check);
		check_hot_reload_review(check);
		check_clone(check);
		check_editor_document(check);
		check_play_mode(check);
		check_ui(check);
		check_physics_scripts(check);
		check_joint_scripts(check);
		check_stub_globals(check);
		check_component_stubs(check);
		check_lua_stubs(check, pipeline);

		if (check.Failures == 0)
		{
			WI_SCRIPT_LOG(Info, "Script smoke test passed");
		}
		return check.Failures == 0;
	}
}
