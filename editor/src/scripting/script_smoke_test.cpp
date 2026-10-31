#include <editor_common.h>
#include "script_smoke_test.h"

#include <algorithm>

#include <assets/wasset_io.h>
#include <scene/prefab_expander.h>
#include <scene/reflection/component_registry.h>
#include <scene/reflection/value_bag.h>
#include <scene/scene.h>
#include <scene/scene_records.h>
#include <scripting/script_component.h>
#include <scripting/script_context.h>
#include <scripting/script_instances.h>
#include <scripting/script_requires.h>
#include <scripting/script_runtime.h>

#include "asset_pipeline/asset_database.h"
#include "asset_pipeline/builtin_importers.h"
#include "asset_pipeline/file_utils.h"
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
		check_scene_scripts(check);
		check_lifecycle(check);

		if (check.Failures == 0)
		{
			WI_SCRIPT_LOG(Info, "Script smoke test passed");
		}
		return check.Failures == 0;
	}
}
