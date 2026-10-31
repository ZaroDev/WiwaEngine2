#include <editor_common.h>
#include "script_smoke_test.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

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

#include "asset_pipeline/asset_database.h"
#include "asset_pipeline/build_settings.h"
#include "asset_pipeline/builtin_importers.h"
#include "asset_pipeline/file_utils.h"
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
			check(Packager::pack(database, temp / "out", nullptr, &settings), "pak: packing the build");

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
				check(host.Calls == Calls({ "capture_document", "show_scene(play)", "time_play", "start_scripts" }) && mode.state() == PlayState::Playing, "play_from_edit");
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
				check(host.Calls == Calls({ "stop_scripts", "cancel_loads", "time_stop", "show_scene(edit)", "restore_document" }) && host.LastRestored == host.Edit && host.Active == host.Edit
					&& mode.state() == PlayState::Edit && mode.play_scene() == nullptr, "stop_order_and_the_edit_scene_comes_back");
			}

			{
				FakeHost host;
				PlayMode mode(host);
				mode.play();
				mode.pause();
				host.Calls.clear();
				mode.stop();
				check(host.Calls == Calls({ "stop_scripts", "cancel_loads", "time_stop", "show_scene(edit)", "restore_document" }) && mode.state() == PlayState::Edit, "stop_from_paused");
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
				const bool followed = mode.play_scene() == loaded && host.count("start_scripts") == 1 && host.count("stop_scripts") == 1;
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

		if (check.Failures == 0)
		{
			WI_SCRIPT_LOG(Info, "Script smoke test passed");
		}
		return check.Failures == 0;
	}
}
