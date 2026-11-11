#include <editor_common.h>
#include "scene_smoke_test.h"

#include <scene/components.h>
#include <assets/wasset_io.h>
#include <assets/asset_manager.h>
#include <core/engine.h>
#include <scene/entity_guid.h>
#include <scene/prefab_expander.h>
#include <scene/prefab_node_id.h>
#include <scene/reflection/component_registry.h>
#include <scene/reflection/value_bag.h>
#include <scene/scene.h>
#include <scene/decal_lifetime.h>
#include <scene/particle_runtime.h>
#include <scene/scene_clone.h>
#include <scene/scene_components.h>
#include <scene/scene_loader.h>
#include <scene/scene_module.h>
#include <scene/scene_records.h>

#include "scene_editor.h"
#include "scene_json.h"

#define WI_SCENE_LOG(level, ...) WI_LOG(level, "Scene", __VA_ARGS__)

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
					WI_SCENE_LOG(Error, "Scene smoke test FAILED: {0}", what);
				}
			}
		};

		bool approx_equal(const glm::vec3& a, const glm::vec3& b)
		{
			return glm::length(a - b) < 1e-4f;
		}

		void check_scene_basics(Check& check)
		{
			const EntityGuid a = EntityGuid::generate();
			const EntityGuid b = EntityGuid::generate();
			check(a.valid() && b.valid() && a != b, "generated entity guids are valid and differ");
			check(EntityGuid::from_string(a.to_string()) == a, "an entity guid survives a string round trip");
			check(!EntityGuid::from_string("not a guid").has_value(), "a bad guid string is rejected");
			check(EntityGuid::derive(a, 1) == EntityGuid::derive(a, 1), "derive is deterministic");
			check(EntityGuid::derive(a, 1) != EntityGuid::derive(a, 2), "derive depends on the key");
			check(EntityGuid::derive(a, 1) != EntityGuid::derive(b, 1), "derive depends on the parent");

			Scene scene;
			const EntityGuid rootId = EntityGuid::generate();
			const entt::entity root = scene.create(rootId, "Root");
			const entt::entity child = scene.create("Child", root);
			check(scene.find(rootId) == root, "find returns the entity of a guid");
			check(scene.find(EntityGuid::generate()) == entt::null, "find returns null for an unknown guid");
			check(scene.registry().get<Name>(child).Value == "Child", "an entity keeps its name");

			scene.registry().get<Transform>(root).Position = glm::vec3(1, 0, 0);
			scene.registry().get<Transform>(root).Scale = glm::vec3(2, 2, 2);
			scene.registry().get<Transform>(child).Position = glm::vec3(0, 2, 0);
			scene.update_world_transforms();
			const glm::vec3 world = glm::vec3(scene.registry().get<WorldTransform>(child).Matrix[3]);
			check(approx_equal(world, glm::vec3(1, 4, 0)), "a child's world position goes through its parent's scale");

			const entt::entity other = scene.create("Other");
			check(scene.set_parent(child, other), "a child can move to another parent");
			auto count_children = [&](entt::entity parent)
			{
				u32 count = 0;
				for (entt::entity e = scene.registry().get<Hierarchy>(parent).FirstChild; e != entt::null; e = scene.registry().get<Hierarchy>(e).NextSibling)
				{
					count++;
				}
				return count;
			};
			check(count_children(other) == 1 && count_children(root) == 0, "moving a child keeps the sibling links consistent");

			check(!scene.set_parent(other, child), "a parent cycle is refused");
			check(scene.registry().get<Hierarchy>(other).Parent == entt::null && count_children(other) == 1, "a refused reparent leaves the hierarchy unchanged");

			scene.destroy(other);
			check(!scene.registry().valid(other) && !scene.registry().valid(child), "destroying an entity destroys its subtree");
			check(scene.find(rootId) == root, "destroying another subtree leaves the rest alone");

			// the order of the top-level entities is the order they were made in, whatever is destroyed in between
			Scene ordered;
			const entt::entity first = ordered.create("First");
			const entt::entity second = ordered.create("Second");
			const entt::entity third = ordered.create("Third");
			ordered.destroy(first);
			check(ordered.roots() == std::vector<entt::entity>({ second, third }), "destroying a top-level entity keeps the order of the others");
			const entt::entity fourth = ordered.create("Fourth");
			check(ordered.set_parent(second, fourth) && ordered.roots() == std::vector<entt::entity>({ third, fourth }), "moving a top-level entity under another takes it out of the roots");
			check(ordered.set_parent(second, entt::null) && ordered.roots() == std::vector<entt::entity>({ third, fourth, second }), "moving an entity to the top level puts it last");
		}

		void check_reflection(Check& check)
		{
			register_scene_components();
			ComponentRegistry& components = ComponentRegistry::get();

			const ComponentInfo* transform = components.find("Transform");
			check(transform != nullptr, "Transform is registered");
			if (transform == nullptr)
			{
				return;
			}

			check(transform->Fields.size() == 3 && transform->Fields[0].Name == "Position" && transform->Fields[1].Name == "Rotation" && transform->Fields[2].Name == "Scale", "Transform lists Position, Rotation and Scale");
			check(transform->Fields[0].Type == FieldType::Vec3 && transform->Fields[1].Type == FieldType::Quat && transform->Fields[2].Type == FieldType::Vec3, "Transform fields have the right types");
			check(std::get<glm::vec3>(transform->Fields[2].Default) == glm::vec3(1.0f) && std::get<glm::quat>(transform->Fields[1].Default) == glm::quat(1.0f, 0.0f, 0.0f, 0.0f), "Transform defaults come from the struct");
			check(transform->field_index("Scale") == 2 && transform->field_index("Nope") == -1, "field_index finds fields by name");

			Scene scene;
			const entt::entity entity = scene.create("E");
			check(transform->Set(scene.registry(), entity, 0, FieldValue(glm::vec3(3, 4, 5))), "a Vec3 value can be set");
			check(std::get<glm::vec3>(transform->Get(scene.registry(), entity, 0)) == glm::vec3(3, 4, 5), "a set value reads back");
			check(!transform->Set(scene.registry(), entity, 0, FieldValue(1.0f)), "a value of the wrong type is rejected");
			check(std::get<glm::vec3>(transform->Get(scene.registry(), entity, 0)) == glm::vec3(3, 4, 5), "a rejected value changes nothing");
			check(!transform->Set(scene.registry(), entity, 99, FieldValue(1.0f)), "a field index out of range is rejected");

			const ComponentInfo* name = components.find("Name");
			check(name != nullptr && name->Has(scene.registry(), entity), "Has sees the Name that create() added");
			name->Remove(scene.registry(), entity);
			check(!name->Has(scene.registry(), entity), "Remove takes the component off");
			name->Add(scene.registry(), entity);
			check(name->Has(scene.registry(), entity), "Add puts it back");

			// A component that only exists at run time, like the properties of a Lua script.
			const std::string scriptName = "Script:test.lua";
			components.register_component(make_value_bag_info(scriptName, { FieldDescriptor{ "speed", FieldType::Float, FieldMeta{}, FieldValue(5.0f) } }));
			const ComponentInfo* script = components.find(scriptName);
			check(script != nullptr, "a value-bag component can be registered");
			if (script == nullptr)
			{
				return;
			}

			check(!script->Has(scene.registry(), entity), "an entity does not have the script before Add");
			script->Add(scene.registry(), entity);
			check(script->Has(scene.registry(), entity) && std::get<float>(script->Get(scene.registry(), entity, 0)) == 5.0f, "Add gives the field its default");
			check(script->Set(scene.registry(), entity, 0, FieldValue(9.0f)) && std::get<float>(script->Get(scene.registry(), entity, 0)) == 9.0f, "a bag field can be set");
			check(!script->Set(scene.registry(), entity, 0, FieldValue(std::string("fast"))), "a bag field rejects the wrong type");

			// The script changed: a field that kept its name and type keeps its value, one that changed type is reset.
			components.register_component(make_value_bag_info(scriptName, {
				FieldDescriptor{ "speed", FieldType::Int, FieldMeta{}, FieldValue(i32(1)) },
				FieldDescriptor{ "mass", FieldType::Float, FieldMeta{}, FieldValue(2.0f) } }));
			reconcile_bag(scene.registry(), *components.find(scriptName));
			script = components.find(scriptName);
			check(std::get<i32>(script->Get(scene.registry(), entity, 0)) == 1, "a field that changed type goes back to its default");
			check(std::get<float>(script->Get(scene.registry(), entity, 1)) == 2.0f, "a new field starts at its default");
			script->Set(scene.registry(), entity, 1, FieldValue(7.0f));
			components.register_component(make_value_bag_info(scriptName, {
				FieldDescriptor{ "speed", FieldType::Int, FieldMeta{}, FieldValue(i32(1)) },
				FieldDescriptor{ "mass", FieldType::Float, FieldMeta{}, FieldValue(2.0f) },
				FieldDescriptor{ "extra", FieldType::Bool, FieldMeta{}, FieldValue(true) } }));
			reconcile_bag(scene.registry(), *components.find(scriptName));
			check(std::get<float>(components.find(scriptName)->Get(scene.registry(), entity, 1)) == 7.0f, "a field that kept name and type keeps its value");

			components.unregister_component(scriptName);
			check(components.find(scriptName) == nullptr, "an unregistered component is gone");
			check(std::none_of(components.all().begin(), components.all().end(), [&](const ComponentInfo* info) { return info->Name == scriptName; }), "all() no longer lists it");
		}

		std::vector<std::string> field_names(const ComponentInfo& info)
		{
			std::vector<std::string> names;
			for (const FieldDescriptor& field : info.Fields)
			{
				names.push_back(field.Name);
			}
			return names;
		}

		void check_render_components(Check& check)
		{
			register_scene_components();
			ComponentRegistry& components = ComponentRegistry::get();
			Scene scene;
			const entt::entity entity = scene.create("E");

			const ComponentInfo* camera = components.find("Camera");
			check(camera != nullptr && field_names(*camera) == std::vector<std::string>{ "Projection", "FovY", "Near", "Far", "OrthoSize", "Primary" }, "Camera lists its fields");
			if (camera != nullptr && camera->Fields.size() == 6)
			{
				check(camera->Fields[0].Type == FieldType::Enum && camera->Fields[0].Meta.EnumNames.size() == 2, "Camera.Projection is an enum with two names");
				check(std::get<float>(camera->Fields[1].Default) == 60.0f && std::get<float>(camera->Fields[2].Default) == 0.1f && std::get<float>(camera->Fields[3].Default) == 1000.0f, "Camera defaults");
				check(std::get<bool>(camera->Fields[5].Default) == false, "a camera is not primary by default");
			}

			const ComponentInfo* light = components.find("Light");
			check(light != nullptr && field_names(*light) == std::vector<std::string>{ "Kind", "Color", "Intensity", "Range", "InnerAngle", "OuterAngle", "CastsShadows" }, "Light lists its fields");
			if (light != nullptr && light->Fields.size() == 7)
			{
				check(light->Fields[1].Type == FieldType::Color && std::get<glm::vec4>(light->Fields[1].Default) == glm::vec4(1.0f), "Light.Color is a white color");
				check(std::get<float>(light->Fields[2].Default) == 1.0f && std::get<float>(light->Fields[3].Default) == 10.0f, "Light defaults");

				light->Add(scene.registry(), entity);
				check(light->Set(scene.registry(), entity, 0, FieldValue(i32(2))) && std::get<i32>(light->Get(scene.registry(), entity, 0)) == 2, "Light.Kind takes the last enum value");
				check(!light->Set(scene.registry(), entity, 0, FieldValue(i32(7))), "Light.Kind rejects a value past the enum");
				check(!light->Set(scene.registry(), entity, 0, FieldValue(i32(-1))), "Light.Kind rejects a negative value");
				check(std::get<i32>(light->Get(scene.registry(), entity, 0)) == 2, "a rejected enum value changes nothing");
			}

			const ComponentInfo* mesh = components.find("MeshRenderer");
			check(mesh != nullptr && field_names(*mesh) == std::vector<std::string>{ "Mesh", "Materials", "ReceivesDecals" }, "MeshRenderer lists its fields");
			if (mesh != nullptr && mesh->Fields.size() == 3)
			{
				check(mesh->Fields[0].Type == FieldType::Asset && mesh->Fields[0].Meta.Asset == AssetType::Mesh, "MeshRenderer.Mesh accepts a Mesh asset");
				check(mesh->Fields[1].Type == FieldType::AssetList && mesh->Fields[1].Meta.Asset == AssetType::Material, "MeshRenderer.Materials is a list of Material assets");

				mesh->Add(scene.registry(), entity);
				const std::vector<AssetRef> refs{ AssetRef{ AssetGuid::generate(), 0 }, AssetRef{ AssetGuid::generate(), 3 } };
				check(mesh->Set(scene.registry(), entity, 1, FieldValue(refs)), "an asset list can be set");
				check(std::get<std::vector<AssetRef>>(mesh->Get(scene.registry(), entity, 1)) == refs, "an asset list reads back");
				check(!mesh->Set(scene.registry(), entity, 1, FieldValue(refs[0])), "a single asset is not a list");
			}

			const ComponentInfo* prefab = components.find("PrefabInstance");
			check(prefab != nullptr && field_names(*prefab) == std::vector<std::string>{ "Prefab" }, "PrefabInstance lists its Prefab field only (the overrides are saved by the record layer)");
			if (prefab != nullptr && !prefab->Fields.empty())
			{
				check(prefab->Fields[0].Meta.Asset == AssetType::Prefab, "PrefabInstance.Prefab accepts a Prefab asset");
			}
		}

		const FieldRecord* find_field(const ComponentRecord& component, const std::string& name)
		{
			for (const FieldRecord& field : component.Fields)
			{
				if (field.Name == name)
				{
					return &field;
				}
			}
			return nullptr;
		}

		const ComponentRecord* find_component(const EntityRecord& entity, const std::string& name)
		{
			for (const ComponentRecord& component : entity.Components)
			{
				if (component.Name == name)
				{
					return &component;
				}
			}
			return nullptr;
		}

		void check_scene_data(Check& check)
		{
			register_scene_components();
			const AssetRef mesh{ AssetGuid::generate(), 0 };
			const AssetRef materialA{ AssetGuid::generate(), 2 };
			const AssetRef materialB{ AssetGuid::generate(), 0 };
			const AssetRef prefab{ AssetGuid::generate(), 0 };
			const AssetRef texture{ AssetGuid::generate(), 0 };
			const AssetRef uiPanel{ AssetGuid::generate(), 0 };

			Scene source;
			const EntityGuid rootId = EntityGuid::generate();
			const entt::entity root = source.create(rootId, "Root");
			source.registry().get<Transform>(root).Position = glm::vec3(1, 2, 3);
			source.registry().emplace<Camera>(root, Camera{ ProjectionKind::Orthographic, 45.0f, 0.5f, 200.0f, 7.0f, true });
			Light light;
			light.Kind = LightKind::Spot;
			light.Color = glm::vec4(0.1f, 0.2f, 0.3f, 1.0f);
			light.CastsShadows = true;
			source.registry().emplace<Light>(root, light);
			source.registry().emplace<MeshRenderer>(root, MeshRenderer{ mesh, {} });

			const entt::entity child = source.create(EntityGuid::generate(), "Child", root);
			source.registry().emplace<MeshRenderer>(child, MeshRenderer{ mesh, { materialA, materialB } });
			PrefabInstance instance;
			instance.Prefab = prefab;
			PrefabOverride set;
			set.Kind = OverrideKind::SetField;
			set.NodeId = 5;
			set.Component = "Transform";
			set.Field = "Position";
			set.Value = glm::vec3(9, 8, 7);
			PrefabOverride add;
			add.Kind = OverrideKind::AddChild;
			add.NodeId = 5;
			add.Child.Id = EntityGuid::generate();
			add.Child.Parent = EntityGuid::derive(rootId, 5);
			add.Child.Components.push_back(ComponentRecord{ "MeshRenderer", { FieldRecord{ "Mesh", FieldValue(texture) } } });
			instance.Overrides = { set, add };
			source.registry().emplace<PrefabInstance>(child, instance);
			source.registry().emplace<UICanvas>(child, UICanvas{ uiPanel, 3, false });

			const SceneData first = data_from_scene(source, "test");
			check(first.Entities.size() == 2 && first.Entities[0].Id == rootId && first.Entities[1].Parent == rootId, "a scene becomes records with parents before children");

			const std::vector<std::byte> bytes = serialize(first);
			std::optional<SceneData> back = deserialize<SceneData>(bytes);
			check(back.has_value(), "a scene payload reads back");
			if (!back)
			{
				return;
			}

			Scene copy;
			const ApplyReport report = apply_scene_data(copy, *back);
			check(report.UnknownComponents == 0 && report.UnknownFields == 0, "nothing in a saved scene is unknown");
			const SceneData second = data_from_scene(copy, "test");
			check(serialize(second) == bytes, "save, load, save gives the same bytes");

			const entt::entity copyRoot = copy.find(rootId);
			check(copyRoot != entt::null && copy.registry().get<Camera>(copyRoot).Primary && copy.registry().get<Camera>(copyRoot).Projection == ProjectionKind::Orthographic, "a camera survives");
			check(copy.registry().get<Light>(copyRoot).Kind == LightKind::Spot && copy.registry().get<Light>(copyRoot).Color == glm::vec4(0.1f, 0.2f, 0.3f, 1.0f) && copy.registry().get<Light>(copyRoot).CastsShadows, "a light survives");
			check(copy.registry().get<Transform>(copyRoot).Position == glm::vec3(1, 2, 3), "a transform survives");
			const entt::entity copyChild = copy.find(first.Entities[1].Id);
			check(copyChild != entt::null && copy.registry().get<Hierarchy>(copyChild).Parent == copyRoot, "the hierarchy survives");
			check(copy.registry().get<MeshRenderer>(copyChild).Materials == std::vector<AssetRef>({ materialA, materialB }), "a material list survives");
			check(copy.registry().get<PrefabInstance>(copyChild).Overrides.size() == 2 && copy.registry().get<PrefabInstance>(copyChild).Overrides[0].Value == FieldValue(glm::vec3(9, 8, 7)), "prefab overrides survive");
			check(copy.registry().all_of<UICanvas>(copyChild) && copy.registry().get<UICanvas>(copyChild).Panel == uiPanel && copy.registry().get<UICanvas>(copyChild).SortOrder == 3 && !copy.registry().get<UICanvas>(copyChild).Visible, "a UICanvas survives");

			const std::vector<AssetRef> references = collect_references(first);
			check(references.size() == 6, "collect_references finds each referenced asset once (found " + std::to_string(references.size()) + ")");
			auto has_reference = [&](const AssetRef& ref) { return std::find(references.begin(), references.end(), ref) != references.end(); };
			check(has_reference(mesh) && has_reference(materialA) && has_reference(materialB) && has_reference(prefab) && has_reference(texture) && has_reference(uiPanel), "collect_references names the mesh, materials, prefab, UI panel and the ref inside an override");

			// Things this engine does not know are kept, not dropped.
			SceneData unknown;
			EntityRecord entity;
			entity.Id = EntityGuid::generate();
			entity.Components.push_back(ComponentRecord{ "Spin", { FieldRecord{ "rate", FieldValue(2.0f) } } });
			entity.Components.push_back(ComponentRecord{ "Transform", { FieldRecord{ "Position", FieldValue(glm::vec3(4, 5, 6)) }, FieldRecord{ "wobble", FieldValue(1.0f) } } });
			unknown.Entities.push_back(entity);
			Scene unknownScene;
			const ApplyReport unknownReport = apply_scene_data(unknownScene, unknown);
			check(unknownReport.UnknownComponents == 1 && unknownReport.UnknownFields == 1, "an unknown component and an unknown field are counted");
			check(unknownScene.registry().get<Transform>(unknownScene.find(entity.Id)).Position == glm::vec3(4, 5, 6), "the known fields next to unknown ones are applied");
			const SceneData unknownOut = data_from_scene(unknownScene, "unknown");
			const ComponentRecord* spin = unknownOut.Entities.empty() ? nullptr : find_component(unknownOut.Entities[0], "Spin");
			const ComponentRecord* transformRecord = unknownOut.Entities.empty() ? nullptr : find_component(unknownOut.Entities[0], "Transform");
			check(spin != nullptr && find_field(*spin, "rate") != nullptr && std::get<float>(find_field(*spin, "rate")->Value) == 2.0f, "an unknown component is saved again");
			check(transformRecord != nullptr && find_field(*transformRecord, "wobble") != nullptr && find_field(*transformRecord, "Position") != nullptr, "an unknown field of a known component is saved again next to the known ones");

			// A field of the wrong type is reported and leaves the default.
			SceneData wrong;
			EntityRecord wrongEntity;
			wrongEntity.Id = EntityGuid::generate();
			wrongEntity.Components.push_back(ComponentRecord{ "Transform", { FieldRecord{ "Position", FieldValue(1.0f) } } });
			wrong.Entities.push_back(wrongEntity);
			Scene wrongScene;
			const ApplyReport wrongReport = apply_scene_data(wrongScene, wrong);
			check(!wrongReport.Warnings.empty() && wrongScene.registry().get<Transform>(wrongScene.find(wrongEntity.Id)).Position == glm::vec3(0.0f), "a field of the wrong type is reported and not applied");

			// The smallest record there is, an entity with nothing but an id, is not mistaken for damaged data.
			SceneData bare;
			for (u32 i = 0; i < 10; i++)
			{
				EntityRecord bareEntity;
				bareEntity.Id = EntityGuid::generate();
				bare.Entities.push_back(bareEntity);
			}
			const std::optional<SceneData> bareBack = deserialize<SceneData>(serialize(bare));
			check(bareBack.has_value() && bareBack->Entities.size() == 10, "a scene of entities that have no components reads back");

			// Damaged data is rejected without a crash or a huge allocation.
			bool truncatedAccepted = false;
			for (size_t length = 0; length < bytes.size(); length += 7)
			{
				truncatedAccepted = truncatedAccepted || deserialize<SceneData>(std::span<const std::byte>(bytes.data(), length)).has_value();
			}
			check(!truncatedAccepted, "a truncated scene payload is rejected");

			BinaryArchive hostile = BinaryArchive::writer();
			std::string emptyName;
			u64 hugeCount = 0xFFFFFFFFull;
			hostile.string(emptyName);
			hostile.value(hugeCount);
			const std::vector<std::byte> hostileBytes = hostile.take();
			const auto start = std::chrono::steady_clock::now();
			const bool hostileAccepted = deserialize<SceneData>(hostileBytes).has_value();
			const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
			check(!hostileAccepted && elapsed.count() < 100, "a payload that claims four billion entities is rejected at once");
		}

		// A source folder inside Assets/ for files the checks write. Removing it re-scans, so the cooked files and the catalog
		// entries of the test files go away too.
		class SmokeAssets
		{
		public:
			explicit SmokeAssets(AssetPipelineModule& pipeline)
				: m_Pipeline(pipeline), m_Folder(pipeline.get_database().get_assets_root() / "_scene_smoke")
			{
				std::error_code error;
				std::filesystem::remove_all(m_Folder, error);
				std::filesystem::create_directories(m_Folder, error);
			}

			~SmokeAssets()
			{
				std::error_code error;
				std::filesystem::remove_all(m_Folder, error);
				m_Pipeline.scan_async()->wait();
			}

			const std::filesystem::path& folder() const { return m_Folder; }

			std::filesystem::path write(const std::string& name, const std::string& text) const
			{
				const std::filesystem::path path = m_Folder / name;
				std::ofstream out(path, std::ios::binary | std::ios::trunc);
				out << text;
				return path;
			}

		private:
			AssetPipelineModule& m_Pipeline;
			std::filesystem::path m_Folder;
		};

		// A glTF with a root "Body" and the children "Wheel", "Wheel" and "Door" (the door holds one triangle).
		std::string test_model_gltf()
		{
			return R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],
"nodes":[{"name":"Body","children":[1,2,3]},{"name":"Wheel"},{"name":"Wheel"},{"name":"Door","mesh":0}],
"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],
"buffers":[{"byteLength":44,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIAAAA="}]})";
		}

		PrefabNode make_node(const std::string& name, i32 parent)
		{
			PrefabNode node;
			node.Name = name;
			node.Parent = parent;
			return node;
		}

		void check_node_ids(Check& check, AssetPipelineModule& pipeline)
		{
			// the rule, on nodes made by hand
			std::vector<PrefabNode> nodes{ make_node("Body", -1), make_node("Wheel", 0), make_node("Wheel", 0), make_node("Door", 0) };
			assign_stable_ids(nodes);
			std::unordered_set<u64> unique;
			for (const PrefabNode& node : nodes)
			{
				unique.insert(node.StableId);
			}
			check(unique.size() == nodes.size() && !unique.contains(0), "every node gets its own id, and none is 0");

			std::vector<PrefabNode> grown = nodes;
			for (PrefabNode& node : grown)
			{
				node.StableId = 0;
			}
			grown.push_back(make_node("Wheel", 0));
			assign_stable_ids(grown);
			check(grown[0].StableId == nodes[0].StableId && grown[1].StableId == nodes[1].StableId && grown[2].StableId == nodes[2].StableId && grown[3].StableId == nodes[3].StableId, "adding a later sibling with the same name does not change the ids of the earlier nodes");
			check(grown[4].StableId != grown[1].StableId && grown[4].StableId != grown[2].StableId, "the new sibling has an id of its own");

			std::vector<PrefabNode> hubs{ make_node("Body", -1), make_node("Wheel", 0), make_node("Door", 0), make_node("Hub", 1), make_node("Hub", 2) };
			assign_stable_ids(hubs);
			check(hubs[3].StableId != hubs[4].StableId, "same-named nodes under different parents get different ids");

			std::vector<PrefabNode> loop{ make_node("A", 1), make_node("B", 0) };
			assign_stable_ids(loop);
			check(loop[0].StableId != 0 && loop[1].StableId != 0 && loop[0].StableId != loop[1].StableId, "a parent loop does not hang the id assignment");

			PrefabData prefab;
			prefab.Name = "ids";
			prefab.Nodes = nodes;
			const std::vector<std::byte> bytes = serialize(prefab);
			std::optional<PrefabData> back = deserialize<PrefabData>(bytes);
			check(back && back->Nodes.size() == nodes.size() && back->Nodes[2].StableId == nodes[2].StableId, "stable ids survive the cooked format");
			prefab.Nodes[1].StableId = 0;
			check(!deserialize<PrefabData>(serialize(prefab)).has_value(), "a prefab with a node id of 0 is rejected");

			// the same, through the real importer
			SmokeAssets assets(pipeline);
			AssetDatabase& database = pipeline.get_database();
			AssetManager& manager = *Engine::get().get_module<AssetManager>();
			const std::filesystem::path model = assets.write("ids_model.gltf", test_model_gltf());
			const std::optional<AssetRef> ref = database.ensure_imported(model);
			check(ref.has_value(), "the test model imports");
			if (!ref)
			{
				return;
			}

			auto load_nodes = [&]() -> std::vector<PrefabNode>
			{
				Handle<PrefabData> handle = manager.load<PrefabData>(*ref);
				manager.wait_idle();
				return handle.get() ? handle.get()->Nodes : std::vector<PrefabNode>{};
			};

			const std::vector<PrefabNode> imported = load_nodes();
			check(imported.size() == 4, "the imported prefab has the four nodes of the model");
			if (imported.size() == 4)
			{
				std::vector<PrefabNode> expected = imported;
				for (PrefabNode& node : expected)
				{
					node.StableId = 0;
				}
				assign_stable_ids(expected);
				bool same = true;
				for (size_t i = 0; i < 4; i++)
				{
					same = same && imported[i].StableId == expected[i].StableId && imported[i].StableId != 0;
				}
				check(same, "the importer gives the nodes the ids the rule gives");
				check(imported[1].StableId != imported[2].StableId, "the two wheels of the model have different ids");
			}

			manager.unload_unused();
			database.reimport(model, true);
			const std::vector<PrefabNode> again = load_nodes();
			bool stable = again.size() == imported.size();
			for (size_t i = 0; stable && i < again.size(); i++)
			{
				stable = again[i].StableId == imported[i].StableId;
			}
			check(stable, "a forced re-import gives the same node ids");
		}

		const FieldValue* find_field(const std::vector<ComponentRecord>& components, std::string_view component, std::string_view field)
		{
			for (const ComponentRecord& candidate : components)
			{
				if (candidate.Name != component)
				{
					continue;
				}
				for (const FieldRecord& value : candidate.Fields)
				{
					if (value.Name == field)
					{
						return &value.Value;
					}
				}
			}
			return nullptr;
		}

		void check_node_components(Check& check)
		{
			register_scene_components();

			PrefabData prefab;
			prefab.Name = "lamp";
			PrefabNode lamp = make_node("Lamp", -1);
			lamp.Components.push_back(ComponentRecord{ "Light", { FieldRecord{ "Intensity", FieldValue(5.0f) } } });
			prefab.Nodes = { lamp };
			assign_stable_ids(prefab.Nodes);
			const u64 nodeId = prefab.Nodes[0].StableId;

			const std::vector<std::byte> bytes = serialize(prefab);
			std::optional<PrefabData> back = deserialize<PrefabData>(bytes);
			const FieldValue* intensity = back && back->Nodes.size() == 1 ? find_field(back->Nodes[0].Components, "Light", "Intensity") : nullptr;
			check(intensity != nullptr && std::get_if<float>(intensity) != nullptr && *std::get_if<float>(intensity) == 5.0f, "node_components: a node's components survive the cooked format");

			EntityRecord root;
			root.Id = EntityGuid::generate();
			const ExpandResult expanded = expand_prefab(root, prefab);
			const FieldValue* expandedIntensity = expanded.Entities.size() == 2 ? find_field(expanded.Entities[1].Components, "Light", "Intensity") : nullptr;
			check(expandedIntensity != nullptr && std::get_if<float>(expandedIntensity) != nullptr && *std::get_if<float>(expandedIntensity) == 5.0f, "node_components: expanding the prefab gives the node its components");
			check(find_field(expanded.Entities[1].Components, "Name", "Value") != nullptr && find_field(expanded.Entities[1].Components, "Transform", "Position") != nullptr, "node_components: the node keeps its Name and Transform");

			const std::optional<FieldValue> value = prefab_value(prefab, nodeId, "Light", "Intensity");
			check(value && std::get_if<float>(&*value) != nullptr && *std::get_if<float>(&*value) == 5.0f, "node_components: the prefab's value of a component field is found for override checks");

			// a prefab cooked before nodes had components ends after the bounds: it still loads, with none
			PrefabData plain;
			plain.Name = "plain";
			plain.Nodes = { make_node("A", -1), make_node("B", 0) };
			assign_stable_ids(plain.Nodes);
			std::vector<std::byte> old = serialize(plain);
			old.resize(old.size() - 2 * sizeof(u64) - sizeof(u32)); // the empty component list of each node, and the count of nested instances after them
			std::optional<PrefabData> loadedOld = deserialize<PrefabData>(old);
			check(loadedOld && loadedOld->Nodes.size() == 2 && loadedOld->Nodes[0].Components.empty(), "node_components: a prefab without the component section still loads");

			PrefabData crowded = plain;
			crowded.Nodes.resize(1);
			for (u32 i = 0; i < SCENE_MAX_COMPONENTS_PER_ENTITY + 1; i++)
			{
				crowded.Nodes[0].Components.push_back(ComponentRecord{ "C" + std::to_string(i), {} });
			}
			check(!deserialize<PrefabData>(serialize(crowded)).has_value(), "node_components: more components than a node may have is rejected");
		}

		EntityRecord make_instance(EntityGuid id, AssetRef prefab)
		{
			EntityRecord record;
			record.Id = id;
			record.Components.push_back(ComponentRecord{ "Name", { FieldRecord{ "Value", FieldValue(std::string("Instance")) } } });
			record.Components.push_back(ComponentRecord{ "PrefabInstance", { FieldRecord{ "Prefab", FieldValue(prefab) } } });
			return record;
		}

		PrefabOverride set_field_override(u64 node, const std::string& component, const std::string& field, FieldValue value)
		{
			PrefabOverride override;
			override.Kind = OverrideKind::SetField;
			override.NodeId = node;
			override.Component = component;
			override.Field = field;
			override.Value = std::move(value);
			return override;
		}

		const EntityRecord* find_entity(const std::vector<EntityRecord>& entities, EntityGuid id)
		{
			for (const EntityRecord& entity : entities)
			{
				if (entity.Id == id)
				{
					return &entity;
				}
			}
			return nullptr;
		}

		glm::vec3 position_of(const EntityRecord* entity)
		{
			const ComponentRecord* transform = entity ? find_component(*entity, "Transform") : nullptr;
			const FieldRecord* position = transform ? find_field(*transform, "Position") : nullptr;
			return position ? std::get<glm::vec3>(position->Value) : glm::vec3(-1000.0f);
		}

		void check_prefab(Check& check)
		{
			// compose_node_id: stable, asymmetric, never a reserved value
			{
				bool reserved = false;
				for (u64 i = 0; i < 100; i++)
				{
					for (u64 j = 0; j < 100; j++)
					{
						const u64 id = compose_node_id(i, j);
						reserved = reserved || id == 0 || id == ADDED_CHILD_NODE;
					}
				}
				const u64 edge = compose_node_id(~0ull, ~0ull);
				check(!reserved && edge != 0 && edge != ADDED_CHILD_NODE && compose_node_id(0, 0) != 0, "compose_node_id is never 0 or ADDED_CHILD_NODE");
				check(compose_node_id(3, 5) != compose_node_id(5, 3), "compose_node_id depends on which id is the outer one");
				check(compose_node_id(3, 5) == compose_node_id(3, 5), "compose_node_id is deterministic");
			}

			const AssetRef meshRef{ AssetGuid::generate(), 1 };
			const AssetRef prefabRef{ AssetGuid::generate(), 0 };

			PrefabData prefab;
			prefab.Name = "car";
			prefab.Nodes = { make_node("Body", -1), make_node("Wheel", 0), make_node("Wheel", 0), make_node("Door", 0), make_node("Hub", 2) };
			prefab.Nodes[3].Mesh = meshRef;
			prefab.Nodes[3].Translation = glm::vec3(0, 1, 0);
			assign_stable_ids(prefab.Nodes);
			const u64 bodyNode = prefab.Nodes[0].StableId;
			const u64 wheelNode = prefab.Nodes[1].StableId;
			const u64 wheel2Node = prefab.Nodes[2].StableId;
			const u64 doorNode = prefab.Nodes[3].StableId;

			const EntityGuid instanceId = EntityGuid::generate();
			EntityRecord instance = make_instance(instanceId, prefabRef);

			// the plain expansion
			const ExpandResult plain = expand_prefab(instance, prefab);
			check(plain.Entities.size() == prefab.Nodes.size() + 1 && plain.NodeIds.size() == plain.Entities.size(), "an expansion has the instance root plus one entity per node");
			check(!plain.Entities.empty() && plain.Entities[0].Id == instanceId && plain.NodeIds[0] == 0, "the first record is the instance root");
			bool parentsFirst = true;
			for (size_t i = 0; i < plain.Entities.size(); i++)
			{
				const EntityRecord& entity = plain.Entities[i];
				if (entity.Parent.valid() && (find_entity(plain.Entities, entity.Parent) == nullptr || find_entity(plain.Entities, entity.Parent) > &entity))
				{
					parentsFirst = false;
				}
			}
			check(parentsFirst, "parents come before their children");

			const ExpandResult again = expand_prefab(instance, prefab);
			bool sameIds = again.Entities.size() == plain.Entities.size();
			for (size_t i = 0; sameIds && i < again.Entities.size(); i++)
			{
				sameIds = again.Entities[i].Id == plain.Entities[i].Id;
			}
			check(sameIds, "two expansions give the same entity ids");
			const ExpandResult other = expand_prefab(make_instance(EntityGuid::generate(), prefabRef), prefab);
			check(other.Entities.size() == plain.Entities.size() && other.Entities[1].Id != plain.Entities[1].Id, "two instances of a prefab get different entity ids");

			const EntityGuid bodyId = EntityGuid::derive(instanceId, bodyNode);
			const EntityGuid wheelId = EntityGuid::derive(instanceId, wheelNode);
			const EntityGuid wheel2Id = EntityGuid::derive(instanceId, wheel2Node);
			const EntityGuid doorId = EntityGuid::derive(instanceId, doorNode);
			const EntityRecord* body = find_entity(plain.Entities, bodyId);
			const EntityRecord* door = find_entity(plain.Entities, doorId);
			check(body && body->Parent == instanceId, "a top-level node goes under the instance root");
			check(door && door->Parent == bodyId, "a node goes under its parent node");
			const ComponentRecord* doorMesh = door ? find_component(*door, "MeshRenderer") : nullptr;
			check(doorMesh && find_field(*doorMesh, "Mesh") && std::get<AssetRef>(find_field(*doorMesh, "Mesh")->Value) == meshRef, "a node with a mesh gets a MeshRenderer");
			const EntityRecord* wheel = find_entity(plain.Entities, wheelId);
			check(wheel && !find_component(*wheel, "MeshRenderer"), "a node without a mesh has no MeshRenderer");
			check(position_of(door) == glm::vec3(0, 1, 0), "a node's transform comes from the prefab");

			// each kind of override
			instance.Overrides = { set_field_override(doorNode, "Transform", "Position", FieldValue(glm::vec3(9, 8, 7))) };
			ExpandResult result = expand_prefab(instance, prefab);
			check(position_of(find_entity(result.Entities, doorId)) == glm::vec3(9, 8, 7), "SetField changes a field of a node");
			check(result.Entities[0].Overrides.size() == 1, "the instance root keeps its overrides, so that the scene can be saved again");

			PrefabOverride addLight;
			addLight.Kind = OverrideKind::AddComponent;
			addLight.NodeId = doorNode;
			addLight.Added = ComponentRecord{ "Light", { FieldRecord{ "Intensity", FieldValue(5.0f) } } };
			PrefabOverride removeMesh;
			removeMesh.Kind = OverrideKind::RemoveComponent;
			removeMesh.NodeId = doorNode;
			removeMesh.Component = "MeshRenderer";
			instance.Overrides = { addLight, removeMesh };
			result = expand_prefab(instance, prefab);
			const EntityRecord* changedDoor = find_entity(result.Entities, doorId);
			check(changedDoor && find_component(*changedDoor, "Light") && !find_component(*changedDoor, "MeshRenderer"), "AddComponent adds one, RemoveComponent takes one away");

			PrefabOverride addChild;
			addChild.Kind = OverrideKind::AddChild;
			addChild.NodeId = wheel2Node;
			addChild.Child.Id = EntityGuid::generate();
			addChild.Child.Components.push_back(ComponentRecord{ "Name", { FieldRecord{ "Value", FieldValue(std::string("Bolt")) } } });
			instance.Overrides = { addChild };
			result = expand_prefab(instance, prefab);
			check(result.Entities.size() == plain.Entities.size() + 1, "AddChild adds one entity");
			const EntityRecord& bolt = result.Entities.back();
			check(bolt.Parent == wheel2Id && bolt.Id.valid() && bolt.Id != addChild.Child.Id, "the added child goes under its node and gets an id derived from the instance");
			check(expand_prefab(instance, prefab).Entities.back().Id == bolt.Id, "the added child has the same id on every expansion");
			check(expand_prefab(make_instance(EntityGuid::generate(), prefabRef), prefab).Entities.size() == plain.Entities.size(), "an instance without overrides does not get the added child");

			PrefabOverride removeNode;
			removeNode.Kind = OverrideKind::RemoveNode;
			removeNode.NodeId = wheel2Node;
			instance.Overrides = { removeNode };
			result = expand_prefab(instance, prefab);
			check(result.Entities.size() == plain.Entities.size() - 2 && !find_entity(result.Entities, wheel2Id) && !find_entity(result.Entities, EntityGuid::derive(instanceId, prefab.Nodes[4].StableId)), "RemoveNode removes the node and everything below it");

			instance.Overrides = { set_field_override(0, "Transform", "Scale", FieldValue(glm::vec3(2, 2, 2))) };
			result = expand_prefab(instance, prefab);
			const ComponentRecord* rootTransform = find_component(result.Entities[0], "Transform");
			const FieldRecord* rootScale = rootTransform ? find_field(*rootTransform, "Scale") : nullptr;
			check(rootScale && std::get<glm::vec3>(rootScale->Value) == glm::vec3(2, 2, 2), "node id 0 addresses the instance root itself");

			// revert
			instance.Overrides = { set_field_override(doorNode, "Transform", "Position", FieldValue(glm::vec3(9, 8, 7))), set_field_override(doorNode, "Transform", "Scale", FieldValue(glm::vec3(3, 3, 3))) };
			const std::optional<FieldValue> original = prefab_value(prefab, doorNode, "Transform", "Position");
			check(original && std::get<glm::vec3>(*original) == glm::vec3(0, 1, 0), "prefab_value gives the value the prefab itself has");
			check(!prefab_value(prefab, doorNode, "Light", "Intensity").has_value() && !prefab_value(prefab, 12345, "Transform", "Position").has_value(), "prefab_value is empty for a component or node the prefab does not have");
			revert_field(instance, doorNode, "Transform", "Position");
			check(instance.Overrides.size() == 1 && instance.Overrides[0].Field == "Scale", "revert_field removes exactly that override");
			check(position_of(find_entity(expand_prefab(instance, prefab).Entities, doorId)) == glm::vec3(0, 1, 0), "after a revert the node has the prefab's value again");
			revert_all(instance);
			check(instance.Overrides.empty(), "revert_all leaves no override");

			// a node that is gone from the prefab: the override is dropped, the others still apply
			instance.Overrides = { set_field_override(wheel2Node, "Transform", "Position", FieldValue(glm::vec3(1, 1, 1))), set_field_override(doorNode, "Transform", "Position", FieldValue(glm::vec3(4, 4, 4))) };
			PrefabData reduced = prefab;
			reduced.Nodes.erase(reduced.Nodes.begin() + 4); // the hub goes first, it points at the wheel
			reduced.Nodes.erase(reduced.Nodes.begin() + 2); // the second wheel
			assign_stable_ids(reduced.Nodes);
			check(reduced.Nodes[1].StableId == wheelNode && reduced.Nodes[2].StableId == doorNode, "the ids of the other nodes do not change when a node is removed from the prefab");
			result = expand_prefab(instance, reduced);
			check(result.DroppedOverrideNodes == std::vector<u64>{ wheel2Node } && !result.Warnings.empty(), "an override on a node that is gone is reported and dropped");
			check(position_of(find_entity(result.Entities, doorId)) == glm::vec3(4, 4, 4), "the other overrides still apply");

			// unpack
			instance.Overrides = { set_field_override(doorNode, "Transform", "Position", FieldValue(glm::vec3(9, 8, 7))) };
			const std::vector<EntityRecord> unpacked = unpack(instance, prefab);
			check(unpacked.size() == plain.Entities.size() && !find_component(unpacked[0], "PrefabInstance") && unpacked[0].Overrides.empty(), "unpack flattens the instance into plain entities");
			check(position_of(find_entity(unpacked, doorId)) == glm::vec3(9, 8, 7), "unpack keeps the overridden values");
		}

		// A scene with every kind of value, an unknown component, an unknown field and every kind of override.
		SceneData build_json_sample()
		{
			SceneData data;
			data.Name = "json sample";

			EntityRecord a;
			a.Id = EntityGuid::generate();
			a.Components.push_back(ComponentRecord{ "Name", { FieldRecord{ "Value", FieldValue(std::string("Root \"quoted\"")) } } });
			a.Components.push_back(ComponentRecord{ "Transform", {
				FieldRecord{ "Position", FieldValue(glm::vec3(1.5f, -2.0f, 0.1f)) },
				FieldRecord{ "Rotation", FieldValue(glm::quat(0.5f, 0.5f, 0.5f, 0.5f)) },
				FieldRecord{ "Scale", FieldValue(glm::vec3(1.0f)) },
				FieldRecord{ "wobble", FieldValue(0.25f) } } });
			a.Components.push_back(ComponentRecord{ "Camera", {
				FieldRecord{ "Projection", FieldValue(i32(1)) },
				FieldRecord{ "FovY", FieldValue(45.0f) },
				FieldRecord{ "Primary", FieldValue(true) } } });
			a.Components.push_back(ComponentRecord{ "Light", {
				FieldRecord{ "Kind", FieldValue(i32(2)) },
				FieldRecord{ "Color", FieldValue(glm::vec4(0.1f, 0.2f, 0.3f, 1.0f)) },
				FieldRecord{ "CastsShadows", FieldValue(true) } } });
			a.Components.push_back(ComponentRecord{ "Spin", {
				FieldRecord{ "rate", FieldValue(2.5f) },
				FieldRecord{ "count", FieldValue(i32(-3)) },
				FieldRecord{ "on", FieldValue(false) },
				FieldRecord{ "label", FieldValue(std::string("0123456789abcdef0123456789abcdef")) },
				FieldRecord{ "axis", FieldValue(glm::vec3(0, 1, 0)) },
				FieldRecord{ "tint", FieldValue(glm::vec4(1, 0, 0, 1)) },
				FieldRecord{ "turn", FieldValue(glm::quat(1, 0, 0, 0)) },
				FieldRecord{ "uv", FieldValue(glm::vec2(0.5f, 2.0f)) },
				FieldRecord{ "ref", FieldValue(AssetRef{ AssetGuid::generate(), 7 }) },
				FieldRecord{ "refs", FieldValue(std::vector<AssetRef>{ AssetRef{ AssetGuid::generate(), 0 }, AssetRef{} }) },
				FieldRecord{ "target", FieldValue(EntityGuid::generate()) } } });
			data.Entities.push_back(a);

			EntityRecord b;
			b.Id = EntityGuid::generate();
			b.Parent = a.Id;
			b.Components.push_back(ComponentRecord{ "Name", { FieldRecord{ "Value", FieldValue(std::string("Child")) } } });
			b.Components.push_back(ComponentRecord{ "MeshRenderer", {
				FieldRecord{ "Mesh", FieldValue(AssetRef{ AssetGuid::generate(), 3 }) },
				FieldRecord{ "Materials", FieldValue(std::vector<AssetRef>{ AssetRef{ AssetGuid::generate(), 0 }, AssetRef{ AssetGuid::generate(), 9 } }) } } });
			b.Components.push_back(ComponentRecord{ "PrefabInstance", { FieldRecord{ "Prefab", FieldValue(AssetRef{ AssetGuid::generate(), 0 }) } } });

			const u64 node = 0x1122334455667788ull;
			b.Overrides.push_back(set_field_override(node, "Transform", "Position", FieldValue(glm::vec3(9, 8, 7))));
			b.Overrides.push_back(set_field_override(node, "Spin", "rate", FieldValue(4.0f)));
			PrefabOverride addComponent;
			addComponent.Kind = OverrideKind::AddComponent;
			addComponent.NodeId = node;
			addComponent.Added = ComponentRecord{ "Light", { FieldRecord{ "Intensity", FieldValue(3.0f) } } };
			b.Overrides.push_back(addComponent);
			PrefabOverride removeComponent;
			removeComponent.Kind = OverrideKind::RemoveComponent;
			removeComponent.NodeId = node;
			removeComponent.Component = "MeshRenderer";
			b.Overrides.push_back(removeComponent);
			PrefabOverride addChild;
			addChild.Kind = OverrideKind::AddChild;
			addChild.NodeId = node;
			addChild.Child.Id = EntityGuid::generate();
			addChild.Child.Components.push_back(ComponentRecord{ "Name", { FieldRecord{ "Value", FieldValue(std::string("Bolt")) } } });
			b.Overrides.push_back(addChild);
			PrefabOverride removeNode;
			removeNode.Kind = OverrideKind::RemoveNode;
			removeNode.NodeId = 42;
			b.Overrides.push_back(removeNode);
			data.Entities.push_back(b);
			return data;
		}

		SceneSettings smoke_settings()
		{
			SceneSettings s;
			s.Source = EnvironmentSource::Atmosphere;
			s.EnvironmentAsset = AssetRef{ AssetGuid{ 5, 6 }, 7 };
			s.RotationDegrees = 45.0f;
			s.Brightness = 1.5f;
			s.AmbientIntensity = 0.6f;
			s.HemisphereSky = glm::vec3(0.2f, 0.3f, 0.4f);
			s.Atmosphere.RayleighScale = 2.0f;
			s.Atmosphere.GroundAlbedo = glm::vec3(0.3f);
			s.Background = BackgroundMode::SolidColor;
			s.BackgroundColor = glm::vec3(0.5f, 0.25f, 0.125f);
			s.ExposureEv = 1.5f;
			s.Exposure.Auto = true;
			s.Exposure.MaxEv = 6.0f;
			s.Bloom.Enabled = true;
			s.Bloom.Intensity = 0.1f;
			s.Tonemap.Operator = TonemapOperator::Reinhard;
			s.Tonemap.Vignette = 0.5f;
			s.Ao.Radius = 2.0f;
			s.Taa.Feedback = 0.85f;
			s.MotionBlur = { true, 0.02f, 2.0f, 50.0f };
			return s;
		}

		void check_scene_settings_json(Check& check)
		{
			SceneData plain;
			plain.Name = "plain";
			check(!scene_to_json(plain).contains("settings"), "default settings are not written");
			const SceneJson noSettings = scene_to_json(plain);
			const SceneJsonResult old = scene_from_json(noSettings);
			check(old.Scene && old.Scene->Settings == SceneSettings{}, "a file without settings has the defaults");

			SceneData with = plain;
			with.Settings = smoke_settings();
			const SceneJson doc = scene_to_json(with);
			check(doc.contains("settings") && doc["settings"]["source"] == "atmosphere" && doc["settings"]["background"] == "color", "enums are written by name");
			check(!doc["settings"].contains("ambient") == false && !doc["settings"].contains("brightness") == false, "changed values are written");
			check(!doc["settings"].contains("hemisphereGround"), "unchanged values are not");
			const SceneJsonResult back = scene_from_json(SceneJson::parse(doc.dump(2)));
			check(back.Scene && back.Scene->Settings == with.Settings, "settings round-trip through the text of the file (" + back.Error + ")");
			check(back.Scene && serialize(*back.Scene) == serialize(with), "and give the same binary scene");

			auto error_of = [&](const std::function<void(SceneJson&)>& edit)
			{
				SceneJson edited = doc;
				edit(edited);
				const SceneJsonResult result = scene_from_json(edited);
				return result.Scene.has_value() ? std::string() : result.Error;
			};
			auto contains = [](const std::string& text, const char* part) { return text.find(part) != std::string::npos; };
			std::string error = error_of([](SceneJson& j) { j["settings"]["source"] = "bogus"; });
			check(contains(error, "settings.source"), "an unknown source is rejected with its path (" + error + ")");
			error = error_of([](SceneJson& j) { j["settings"]["brightness"] = "x"; });
			check(contains(error, "settings.brightness"), "a value of the wrong type is rejected with its path (" + error + ")");
			error = error_of([](SceneJson& j) { j["settings"]["tonemap"]["operator"] = "bogus"; });
			check(contains(error, "settings.tonemap.operator"), "an unknown tonemap operator is rejected with its path (" + error + ")");
			error = error_of([](SceneJson& j) { j["settings"]["bloom"]["enabled"] = 3; });
			check(contains(error, "settings.bloom.enabled"), "a bloom flag of the wrong type is rejected (" + error + ")");
			error = error_of([](SceneJson& j) { j["settings"]["atmosphere"]["mie"] = true; });
			check(contains(error, "settings.atmosphere.mie"), "so is one inside the atmosphere block (" + error + ")");
			error = error_of([](SceneJson& j) { j["settings"]["taa"]["feedback"] = "x"; });
			check(contains(error, "settings.taa.feedback"), "a TAA feedback of the wrong type is rejected (" + error + ")");
			error = error_of([](SceneJson& j) { j["settings"]["motionBlur"]["enabled"] = 3; });
			check(contains(error, "settings.motionBlur.enabled"), "a motion blur flag of the wrong type is rejected (" + error + ")");
			SceneJson extra = doc;
			extra["settings"]["somethingNew"] = 1;
			check(scene_from_json(extra).Scene.has_value(), "unknown keys are ignored");
			SceneJson wild = doc;
			wild["settings"]["exposureEv"] = 1000.0;
			const SceneJsonResult wildResult = scene_from_json(wild);
			check(wildResult.Scene && wildResult.Scene->Settings.ExposureEv == 10.0f, "an absurd value is clamped");
		}

		void check_scene_json(Check& check, AssetPipelineModule& pipeline)
		{
			check_scene_settings_json(check);
			register_scene_components();
			const SceneData sample = build_json_sample();
			const std::vector<std::byte> sampleBytes = serialize(sample);

			const SceneJson doc = scene_to_json(sample);
			const SceneJsonResult parsed = scene_from_json(doc);
			check(parsed.Scene.has_value(), "the JSON of a scene reads back (" + parsed.Error + ")");
			check(parsed.Scene && serialize(*parsed.Scene) == sampleBytes, "scene to JSON to scene gives the same scene");

			const SceneJsonResult fromText = scene_from_json(SceneJson::parse(doc.dump(2)));
			check(fromText.Scene && serialize(*fromText.Scene) == sampleBytes, "the same through the text of the file");

			// known values are plain JSON, easy to edit by hand
			check(doc["entities"][0]["components"]["Camera"]["Projection"] == "Orthographic", "an enum is written by name");
			check(doc["entities"][0]["components"]["Transform"]["Position"].is_array(), "a vector is written as an array");

			auto error_of = [&](const std::function<void(SceneJson&)>& edit)
			{
				SceneJson edited = doc;
				edit(edited);
				const SceneJsonResult result = scene_from_json(edited);
				return result.Scene.has_value() ? std::string() : result.Error;
			};
			auto contains = [](const std::string& text, const char* part) { return text.find(part) != std::string::npos; };

			std::string error = error_of([](SceneJson& j) { j["entities"][1]["id"] = j["entities"][0]["id"]; });
			check(contains(error, "entities[1].id") && contains(error, "duplicate"), "a duplicate entity id is rejected with its path (" + error + ")");
			error = error_of([](SceneJson& j) { j["entities"][1]["parent"] = EntityGuid::generate().to_string(); });
			check(contains(error, "entities[1].parent"), "a parent that is not in the file is rejected with its path (" + error + ")");
			error = error_of([](SceneJson& j) { j["entities"][0]["parent"] = j["entities"][1]["id"]; });
			check(contains(error, "parent loop"), "a parent cycle is rejected (" + error + ")");
			error = error_of([](SceneJson& j) { j["entities"][1]["id"] = "xyz"; });
			check(contains(error, "entities[1].id"), "an id that is not a guid is rejected (" + error + ")");
			error = error_of([](SceneJson& j) { j["entities"][1]["components"]["MeshRenderer"]["Mesh"] = 5; });
			check(contains(error, "entities[1].MeshRenderer.Mesh") && contains(error, "not an AssetRef"), "a field of the wrong type is rejected with its path (" + error + ")");
			error = error_of([](SceneJson& j) { j["entities"][0]["components"]["Camera"]["Projection"] = "Sideways"; });
			check(contains(error, "entities[0].Camera.Projection"), "an enum name that does not exist is rejected (" + error + ")");
			error = error_of([](SceneJson& j) { j["entities"][1]["overrides"][0]["kind"] = "Explode"; });
			check(contains(error, "overrides[0].kind"), "an unknown override kind is rejected (" + error + ")");
			error = error_of([](SceneJson& j) { j.erase("entities"); });
			check(contains(error, "entities"), "a file without entities is rejected (" + error + ")");

			// through the importer
			SmokeAssets assets(pipeline);
			AssetDatabase& database = pipeline.get_database();
			AssetManager& manager = *Engine::get().get_module<AssetManager>();
			const std::filesystem::path path = assets.write("json_level.wscene", doc.dump(2));
			const std::optional<AssetRef> ref = database.ensure_imported(path);
			check(ref.has_value(), "a .wscene imports (its references to assets that do not exist are fine)");
			if (!ref)
			{
				return;
			}

			auto load_bytes = [&]() -> std::vector<std::byte>
			{
				Handle<SceneData> handle = manager.load<SceneData>(*ref);
				manager.wait_idle();
				return handle.get() ? serialize(*handle.get()) : std::vector<std::byte>{};
			};
			check(load_bytes() == sampleBytes, "the cooked scene loads back as the scene that was written");

			// a broken edit fails the import and the last good cooked scene stays
			WI_SCENE_LOG(Info, "Smoke test: the next import error is expected");
			manager.unload_unused();
			assets.write("json_level.wscene", "{ \"entities\": [ { \"id\": \"nope\" } ] }");
			database.reimport(path);
			check(load_bytes() == sampleBytes, "after a failed re-import the previous cooked scene is still the one in use");
		}

		EntityRecord plain_entity(const std::string& name, glm::vec3 position)
		{
			EntityRecord record;
			record.Id = EntityGuid::generate();
			record.Components.push_back(ComponentRecord{ "Name", { FieldRecord{ "Value", FieldValue(name) } } });
			record.Components.push_back(ComponentRecord{ "Transform", { FieldRecord{ "Position", FieldValue(position) } } });
			return record;
		}

		std::vector<EntityGuid> guids_of(Scene& scene)
		{
			std::vector<EntityGuid> guids;
			for (const entt::entity entity : scene.registry().view<Id>())
			{
				guids.push_back(scene.registry().get<Id>(entity).Guid);
			}
			std::sort(guids.begin(), guids.end());
			return guids;
		}

		size_t entity_count(const Ref<Scene>& scene)
		{
			return scene ? scene->registry().view<Id>().size() : 0;
		}

		void check_blueprint(Check& check)
		{
			const AssetRef prefabRef{ AssetGuid::generate(), 0 };
			const AssetRef meshRef{ AssetGuid::generate(), 1 };
			PrefabData prefab;
			prefab.Nodes = { make_node("Body", -1), make_node("Door", 0) };
			prefab.Nodes[1].Mesh = meshRef;
			assign_stable_ids(prefab.Nodes);

			SceneData data;
			data.Entities.push_back(plain_entity("Before", glm::vec3(0)));
			data.Entities.push_back(make_instance(EntityGuid::generate(), prefabRef));
			data.Entities.push_back(plain_entity("After", glm::vec3(0)));

			SceneBlueprint blueprint = build_blueprint(data, [&](const AssetRef& ref) { return ref == prefabRef ? &prefab : nullptr; });
			check(blueprint.Entities.size() == 5 && blueprint.Origins.size() == 5, "a blueprint has the scene's entities plus the nodes of each instance");
			check(blueprint.Origins[0].Instance == EntityGuid{} && blueprint.Origins[2].Instance == data.Entities[1].Id && blueprint.Origins[2].NodeId == prefab.Nodes[0].StableId, "the entities of an instance are marked with their instance and node");
			check(std::find(blueprint.References.begin(), blueprint.References.end(), meshRef) != blueprint.References.end() && std::find(blueprint.References.begin(), blueprint.References.end(), prefabRef) != blueprint.References.end(), "the references include the prefab and what its nodes use");

			SceneBlueprint noPrefab = build_blueprint(data, [](const AssetRef&) { return nullptr; });
			check(noPrefab.Entities.size() == 3 && !noPrefab.Warnings.empty(), "an instance whose prefab did not load stays as the root alone, with a warning");
		}

		// prefab_instances_use_extracted_materials: an instance's mesh refers to a .wmat file next to the model, and the
		// material that loads is the one the file holds.
		void check_extracted_materials(Check& check, AssetPipelineModule& pipeline)
		{
			register_scene_components();

			SmokeAssets assets(pipeline);
			AssetDatabase& database = pipeline.get_database();
			AssetManager& manager = *Engine::get().get_module<AssetManager>();
			SceneModule& module = *Engine::get().get_module<SceneModule>();

			std::string gltf = test_model_gltf();
			const std::string meshTail = R"("indices":1}]}])";
			const size_t at = gltf.find(meshTail);
			check(at != std::string::npos, "the test model has the expected mesh text");
			if (at == std::string::npos)
			{
				return;
			}
			gltf.replace(at, meshTail.size(), R"("indices":1,"material":0}]}],"materials":[{"name":"Painted","pbrMetallicRoughness":{"baseColorFactor":[0.25,0.5,0.75,1],"roughnessFactor":0.375}}])");

			const std::filesystem::path model = assets.write("mat_model.gltf", gltf);
			const std::optional<AssetRef> prefabRef = database.ensure_imported(model);
			check(prefabRef.has_value(), "the model with a material imports");
			if (!prefabRef)
			{
				return;
			}

			SceneData level;
			level.Name = "mat_level";
			level.Entities = { make_instance(EntityGuid::generate(), *prefabRef) };
			const std::filesystem::path levelPath = assets.write("mat_level.wscene", scene_to_json(level).dump());
			const std::optional<AssetRef> levelRef = database.ensure_imported(levelPath);
			check(levelRef.has_value(), "the level with the model imports");
			if (!levelRef)
			{
				return;
			}

			Ref<JobProgress> progress = module.load_async(*levelRef);
			progress->wait();
			Ref<Scene> loaded = module.get_active();
			AssetRef meshRef{};
			if (loaded)
			{
				for (entt::entity entity : loaded->registry().view<MeshRenderer>())
				{
					meshRef = loaded->registry().get<MeshRenderer>(entity).Mesh;
				}
			}
			check(meshRef.valid(), "the loaded instance has a mesh");
			if (!meshRef.valid())
			{
				return;
			}

			Handle<MeshData> mesh = manager.load<MeshData>(meshRef);
			manager.wait_idle();
			const AssetRef materialRef = (mesh.get() && !mesh.get()->Primitives.empty()) ? mesh.get()->Primitives[0].Material : AssetRef{};
			const std::optional<std::filesystem::path> materialPath = materialRef.valid() ? database.path_of(materialRef.Guid) : std::nullopt;
			check(materialPath && materialPath->extension() == ".wmat" && materialPath->parent_path().filename() == "mat_model_materials",
				"prefab_instances_use_extracted_materials: the mesh refers to a .wmat in the model's material folder");

			Handle<MaterialData> material = manager.load<MaterialData>(materialRef);
			manager.wait_idle();
			check(material.get() && material.get()->RoughnessFactor == 0.375f && material.get()->BaseColorFactor == glm::vec4(0.25f, 0.5f, 0.75f, 1.0f),
				"prefab_instances_use_extracted_materials: the loaded material has the model's values");
		}

		void check_scene_loading(Check& check, AssetPipelineModule& pipeline)
		{
			register_scene_components();
			check_blueprint(check);

			SmokeAssets assets(pipeline);
			AssetDatabase& database = pipeline.get_database();
			AssetManager& manager = *Engine::get().get_module<AssetManager>();
			SceneModule& module = *Engine::get().get_module<SceneModule>();

			auto write_scene = [&](const std::string& name, const SceneData& data)
			{
				const std::filesystem::path path = assets.write(name, scene_to_json(data).dump());
				return database.ensure_imported(path);
			};

			const std::optional<AssetRef> prefabRef = database.ensure_imported(assets.write("load_model.gltf", test_model_gltf()));
			check(prefabRef.has_value(), "the model for the scene imports");
			if (!prefabRef)
			{
				return;
			}

			u64 doorNode = 0;
			{
				Handle<PrefabData> prefab = manager.load<PrefabData>(*prefabRef);
				manager.wait_idle();
				for (const PrefabNode& node : prefab.get() ? prefab.get()->Nodes : std::vector<PrefabNode>{})
				{
					if (node.Name == "Door")
					{
						doorNode = node.StableId;
					}
				}
			}
			check(doorNode != 0, "the model has a Door node");

			SceneData level;
			level.Name = "level";
			EntityRecord camera = plain_entity("Camera", glm::vec3(0, 1, 5));
			camera.Components.push_back(ComponentRecord{ "Camera", { FieldRecord{ "Primary", FieldValue(true) } } });
			EntityRecord light = plain_entity("Sun", glm::vec3(0));
			light.Components.push_back(ComponentRecord{ "Light", { FieldRecord{ "Kind", FieldValue(i32(0)) } } });
			EntityRecord instance = make_instance(EntityGuid::generate(), *prefabRef);
			instance.Overrides = { set_field_override(doorNode, "Transform", "Position", FieldValue(glm::vec3(9, 8, 7))) };
			level.Entities = { camera, light, instance };
			level.Settings = smoke_settings();
			const std::optional<AssetRef> levelRef = write_scene("level.wscene", level);
			check(levelRef.has_value(), "the level imports");
			if (!levelRef)
			{
				return;
			}

			const Ref<Scene> initial = module.get_active();
			check(initial != nullptr && entity_count(initial) == 0, "there is an empty scene before the first load");

			Ref<JobProgress> progress = module.load_async(*levelRef);
			progress->wait();
			check(progress->succeeded(), "a scene loads (" + progress->stage() + ")");
			Ref<Scene> first = module.get_active();
			check(first && first->settings() == sanitized(smoke_settings()), "a loaded scene has the settings of its file");
			check(first != initial && entity_count(first) == 7, "the loaded scene has the scene's entities plus the four nodes (found " + std::to_string(entity_count(first)) + ")");
			if (first && entity_count(first) == 7)
			{
				const entt::entity door = first->find(EntityGuid::derive(instance.Id, doorNode));
				check(door != entt::null && first->registry().get<Transform>(door).Position == glm::vec3(9, 8, 7), "an override of the instance is applied");
				const PrefabChild* tag = door != entt::null ? first->registry().try_get<PrefabChild>(door) : nullptr;
				check(tag && tag->Instance == instance.Id && tag->NodeId == doorNode, "an entity of an instance knows its instance and node");
				check(door != entt::null && first->registry().all_of<MeshRenderer>(door) && first->registry().get<MeshRenderer>(door).Mesh.valid(), "the door has its mesh");
				const entt::entity root = first->find(instance.Id);
				check(root != entt::null && !first->registry().all_of<PrefabChild>(root), "the instance root itself is an authored entity");
			}

			Ref<JobProgress> again = module.load_async(*levelRef);
			again->wait();
			check(again->succeeded() && first && module.get_active() && guids_of(*module.get_active()) == guids_of(*first), "loading again gives the same entity ids");

			// a reference to an asset that does not exist does not fail the load
			WI_SCENE_LOG(Info, "Smoke test: the next asset load errors are expected");
			SceneData dangling;
			EntityRecord broken = plain_entity("Broken", glm::vec3(0));
			const AssetRef missing{ AssetGuid::generate(), 0 };
			broken.Components.push_back(ComponentRecord{ "MeshRenderer", { FieldRecord{ "Mesh", FieldValue(missing) } } });
			dangling.Entities.push_back(broken);
			const std::optional<AssetRef> danglingRef = write_scene("dangling.wscene", dangling);
			check(danglingRef.has_value(), "a scene with a reference to a missing asset imports");
			if (danglingRef)
			{
				Ref<JobProgress> p = module.load_async(*danglingRef);
				p->wait();
				const Ref<Scene> active = module.get_active();
				const entt::entity entity = active ? active->find(broken.Id) : entt::entity{ entt::null };
				check(p->succeeded() && entity != entt::null && active->registry().get<MeshRenderer>(entity).Mesh == missing, "a missing asset does not fail the load and the entity keeps its reference");
			}

			// a scene that cannot be loaded leaves the current one alone
			{
				const Ref<Scene> before = module.get_active();
				const size_t countBefore = entity_count(before);
				Ref<JobProgress> p = module.load_async(AssetRef{ AssetGuid::generate(), 0 });
				p->wait();
				check(p->state() == JobProgress::State::Failed && module.get_active() == before && entity_count(before) == countBefore, "a scene that is missing fails and the current scene is untouched");
				Ref<JobProgress> wrong = module.load_async(*prefabRef);
				wrong->wait();
				check(wrong->state() == JobProgress::State::Failed && module.get_active() == before, "an asset of another type fails to load as a scene");
			}

			// a big scene, loaded under a frame budget
			SceneData big;
			big.Name = "big";
			for (u32 i = 0; i < 20000; i++)
			{
				big.Entities.push_back(plain_entity("E" + std::to_string(i), glm::vec3(static_cast<float>(i), 0, 0)));
			}
			const std::optional<AssetRef> bigRef = write_scene("big.wscene", big);
			check(bigRef.has_value(), "the big scene imports");
			if (!bigRef)
			{
				return;
			}

			{
				module.set_commit_budget_ms(2.0f);
				Ref<JobProgress> p = module.load_async(*bigRef);
				p->wait();
				const SceneLoadStats stats = module.get_last_load_stats();
				WI_SCENE_LOG(Info, "Smoke test: 20000 entities created over {0} updates, the longest took {1} ms", stats.CommitTicks, stats.LongestCommitMs);
				check(p->succeeded() && entity_count(module.get_active()) == 20000, "a big scene loads (found " + std::to_string(entity_count(module.get_active())) + " entities)");
				check(stats.CommitTicks > 1, "the entities were created over several frames (" + std::to_string(stats.CommitTicks) + " ticks)");
				check(stats.LongestCommitMs < 8.0f, "no frame spent more than the budget plus slack creating entities (longest " + std::to_string(stats.LongestCommitMs) + " ms)");
			}

			// a second load cancels the first
			{
				Ref<JobProgress> first_load = module.load_async(*bigRef);
				Ref<JobProgress> second_load = module.load_async(*levelRef);
				first_load->wait();
				second_load->wait();
				check(first_load->state() == JobProgress::State::Failed, "a load that was replaced by another one is cancelled");
				check(second_load->succeeded() && entity_count(module.get_active()) == 7, "the second load completes and its scene is the active one");
			}

			// replacing never leaves a frame without a scene
			{
				Ref<JobProgress> p = module.load_async(*bigRef);
				bool neverEmpty = true;
				while (!p->finished())
				{
					neverEmpty = neverEmpty && module.get_active() != nullptr;
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
				check(neverEmpty && p->succeeded(), "the active scene is never null while another one loads");
			}

			// loads asked for from another thread while the main thread updates: every progress object ends, the last load wins, and
			// nothing is left loading (this thread is not the main thread, which is the case load_async promises to handle)
			{
				module.set_commit_budget_ms(0.2f); // many small slices, so the loads overlap the updates
				std::vector<Ref<JobProgress>> loads;
				for (u32 i = 0; i < 60; i++)
				{
					loads.push_back(module.load_async(i % 3 == 0 ? *bigRef : *levelRef));
					if (i % 4 == 0)
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(3));
					}
				}

				bool allFinished = true;
				for (const Ref<JobProgress>& load : loads)
				{
					load->wait();
					allFinished = allFinished && load->finished();
				}
				check(allFinished && loads.back()->succeeded(), "60 loads in a row from another thread all end, and the last one succeeds");
				check(entity_count(module.get_active()) == 7, "the scene of the last load is the active one");

				bool idle = false;
				for (u32 i = 0; i < 500 && !idle; i++)
				{
					idle = !module.is_loading();
					std::this_thread::sleep_for(std::chrono::milliseconds(2));
				}
				check(idle, "nothing is left loading afterwards");
				module.set_commit_budget_ms(2.0f);
			}

			// additive: the entities join the active scene
			{
				SceneData extra;
				EntityRecord extraEntity = plain_entity("Extra", glm::vec3(0));
				extra.Entities.push_back(extraEntity);
				const std::optional<AssetRef> extraRef = write_scene("extra.wscene", extra);
				const Ref<Scene> active = module.get_active();
				const size_t countBefore = entity_count(active);
				Ref<JobProgress> p = extraRef ? module.load_async(*extraRef, LoadMode::Additive) : Ref<JobProgress>{};
				if (p)
				{
					p->wait();
				}
				check(p && p->succeeded() && module.get_active() == active && entity_count(active) == countBefore + 1 && active->find(extraEntity.Id) != entt::null, "an additive load adds to the active scene");
			}
			module.set_commit_budget_ms(2.0f);
		}

		void check_make_prefab(Check& check, AssetPipelineModule& pipeline)
		{
			register_scene_components();
			SmokeAssets assets(pipeline);
			AssetDatabase& database = pipeline.get_database();
			AssetManager& manager = *Engine::get().get_module<AssetManager>();
			SceneModule& module = *Engine::get().get_module<SceneModule>();

			SceneEditor editor(manager, module);
			editor.set_scene(CreateRef<Scene>(), {});
			Scene& scene = editor.scene();
			entt::registry& registry = scene.registry();
			auto pump = [&]()
			{
				for (u32 i = 0; i < 2000 && (editor.has_pending_prefab() || editor.has_pending()); i++)
				{
					editor.update();
					std::this_thread::sleep_for(std::chrono::milliseconds(2));
				}
			};

			const entt::entity before = editor.create_entity(entt::null);
			const entt::entity lamp = editor.create_entity(entt::null);
			const entt::entity bulb = editor.create_entity(lamp);
			const entt::entity after = editor.create_entity(entt::null);
			registry.get<Name>(lamp).Value = "Lamp";
			registry.get<Name>(bulb).Value = "Bulb";
			registry.get<Transform>(lamp).Position = glm::vec3(1.0f, 2.0f, 3.0f);
			registry.emplace<Light>(lamp).Intensity = 7.0f;
			editor.select(lamp);

			// refusals leave the scene as it was
			const size_t entityCount = registry.storage<Id>().size();
			editor.set_playing(true);
			check(!editor.make_prefab(before, assets.folder()).empty() && !editor.has_pending_prefab(), "make_prefab: refused while the scene plays");
			editor.set_playing(false);
			check(!editor.make_prefab(entt::null, assets.folder()).empty(), "make_prefab: refused for no entity");

			// the entity becomes an instance of the prefab, in the same place
			const std::string refusal = editor.make_prefab(lamp, assets.folder());
			check(refusal.empty() && editor.has_pending_prefab(), "make_prefab: starts for a plain entity (" + refusal + ")");
			check(registry.valid(lamp), "make_prefab: the entity stays until the prefab has been imported");
			database.ensure_imported(assets.folder() / "Lamp.wprefab"); // the editor's file watcher does this
			pump();

			check(!editor.has_pending_prefab() && !registry.valid(lamp) && !registry.valid(bulb), "make_prefab: the original subtree is gone once the prefab is there");
			check(scene.roots().size() == 3 && scene.roots()[0] == before && scene.roots()[2] == after, "make_prefab: the instance takes the place of the entity among its siblings");
			const entt::entity instance = scene.roots().size() == 3 ? scene.roots()[1] : entt::entity{ entt::null };
			check(instance != entt::null && registry.all_of<PrefabInstance>(instance) && registry.get<Name>(instance).Value == "Lamp", "make_prefab: the instance has the entity's name");
			check(instance != entt::null && registry.get<Transform>(instance).Position == glm::vec3(1.0f, 2.0f, 3.0f), "make_prefab: the instance has the entity's transform");
			check(registry.storage<Id>().size() == entityCount - 2 + 3, "make_prefab: an instance root and the two nodes of the prefab replace the two entities (" + std::to_string(registry.storage<Id>().size()) + ")");
			bool hasLight = false;
			entt::entity generated = entt::null;
			for (const entt::entity entity : registry.view<PrefabChild>())
			{
				generated = entity;
				const Light* light = registry.try_get<Light>(entity);
				hasLight = hasLight || (light != nullptr && light->Intensity == 7.0f);
			}
			check(hasLight, "make_prefab: the light of the entity is in the prefab");
			check(editor.selected() == instance && editor.dirty(), "make_prefab: the instance is selected and the scene is changed");

			// what cannot become a prefab
			const size_t afterCount = registry.storage<Id>().size();
			check(instance != entt::null && !editor.make_prefab(instance, assets.folder()).empty(), "make_prefab: an instance is refused");
			check(generated != entt::null && !editor.make_prefab(generated, assets.folder()).empty(), "make_prefab: an entity a prefab made is refused");
			check(!editor.has_pending_prefab() && registry.storage<Id>().size() == afterCount, "make_prefab: refusals change nothing");

			// an entity deleted while the prefab is being imported is not brought back
			check(editor.make_prefab(before, assets.folder()).empty(), "make_prefab: starts for another entity");
			editor.delete_entity(before);
			pump();
			check(!editor.has_pending_prefab() && registry.storage<Id>().size() == afterCount - 1, "make_prefab: a deleted entity is simply dropped from the conversion");

			// a name Windows reserves is made safe, not written to the device
			const entt::entity reserved = editor.create_entity(entt::null);
			registry.get<Name>(reserved).Value = "CON";
			std::error_code fileError;
			check(editor.make_prefab(reserved, assets.folder()).empty() && std::filesystem::exists(assets.folder() / "_CON.wprefab", fileError), "make_prefab: an entity called CON is saved as _CON.wprefab");
			editor.delete_entity(reserved);
			editor.update();
			check(!editor.has_pending_prefab(), "make_prefab: the conversion of a deleted entity ends");

			// a prefab that is not imported in time is given up with a message, and the entity stays
			const entt::entity ghost = editor.create_entity(entt::null);
			registry.get<Name>(ghost).Value = "Ghost";
			check(editor.take_message().empty(), "make_prefab: no message before anything went wrong");
			editor.set_prefab_timeout(std::chrono::milliseconds(0));
			check(editor.make_prefab(ghost, assets.folder()).empty(), "make_prefab: starts for an entity whose file will not be imported");
			editor.update();
			const std::string message = editor.take_message();
			check(!editor.has_pending_prefab() && registry.valid(ghost) && !registry.all_of<PrefabInstance>(ghost), "make_prefab: a prefab that is not imported in time leaves the entity as it was");
			check(message.find("Ghost") != std::string::npos && editor.take_message().empty(), "make_prefab: the user is told, once (" + message + ")");
		}

		void check_duplicate_rename(Check& check, AssetPipelineModule& pipeline)
		{
			register_scene_components();
			SmokeAssets assets(pipeline);
			AssetDatabase& database = pipeline.get_database();
			AssetManager& manager = *Engine::get().get_module<AssetManager>();
			SceneModule& module = *Engine::get().get_module<SceneModule>();

			const std::optional<AssetRef> prefabRef = database.ensure_imported(assets.write("dup_model.gltf", test_model_gltf()));
			check(prefabRef.has_value(), "the model for the duplicate checks imports");
			if (!prefabRef)
			{
				return;
			}

			SceneEditor editor(manager, module);
			editor.set_scene(CreateRef<Scene>(), {});
			Scene& scene = editor.scene();
			entt::registry& registry = scene.registry();
			auto pump = [&]()
			{
				for (u32 i = 0; i < 2000 && editor.has_pending(); i++)
				{
					editor.update();
					std::this_thread::sleep_for(std::chrono::milliseconds(2));
				}
			};

			const entt::entity a = editor.create_entity(entt::null);
			const entt::entity kid = editor.create_entity(a);
			const entt::entity c = editor.create_entity(entt::null);
			registry.get<Name>(a).Value = "Root";
			registry.get<Name>(kid).Value = "Kid";
			registry.get<Transform>(a).Position = glm::vec3(4.0f, 5.0f, 6.0f);
			registry.emplace<Light>(a).Intensity = 3.0f;

			// a subtree is copied with new guids and lands right after the original
			const size_t before = registry.storage<Id>().size();
			const entt::entity copy = editor.duplicate_entity(a);
			check(copy != entt::null && registry.storage<Id>().size() == before + 2, "duplicate: the entity and its child are copied");
			if (copy != entt::null)
			{
				check(registry.get<Name>(copy).Value == "Root (copy)" && registry.get<Transform>(copy).Position == glm::vec3(4.0f, 5.0f, 6.0f), "duplicate: the copy has the name (marked as a copy) and the transform");
				check(registry.all_of<Light>(copy) && registry.get<Light>(copy).Intensity == 3.0f, "duplicate: the components are copied");
				check(registry.get<Id>(copy).Guid != registry.get<Id>(a).Guid, "duplicate: the copy has a guid of its own");
				const entt::entity copiedKid = registry.get<Hierarchy>(copy).FirstChild;
				check(copiedKid != entt::null && copiedKid != kid && registry.get<Name>(copiedKid).Value == "Kid" && registry.get<Id>(copiedKid).Guid != registry.get<Id>(kid).Guid && registry.get<Hierarchy>(copiedKid).Parent == copy,
					"duplicate: the child is copied under the copy, with a guid of its own");
				check(scene.roots().size() == 3 && scene.roots()[0] == a && scene.roots()[1] == copy && scene.roots()[2] == c, "duplicate: the copy comes right after the original");
				check(editor.selected() == copy && editor.dirty(), "duplicate: the copy is selected and the scene is changed");
			}

			// an instance is copied with its overrides, and generates its own nodes
			const entt::entity instance = editor.add_prefab(*prefabRef, entt::null);
			pump();
			const size_t generatedBefore = registry.view<PrefabChild>().size();
			check(generatedBefore == 4, "duplicate: the model's instance has its four nodes");
			const entt::entity instanceCopy = editor.duplicate_entity(instance);
			pump();
			check(instanceCopy != entt::null && registry.all_of<PrefabInstance>(instanceCopy) && registry.view<PrefabChild>().size() == generatedBefore * 2, "duplicate: a copied instance generates its own nodes (" + std::to_string(registry.view<PrefabChild>().size()) + ")");

			// what cannot be copied or renamed
			entt::entity generated = entt::null;
			for (const entt::entity entity : registry.view<PrefabChild>())
			{
				generated = entity;
			}
			const size_t count = registry.storage<Id>().size();
			check(generated != entt::null && editor.duplicate_entity(generated) == entt::null && editor.duplicate_entity(entt::null) == entt::null && registry.storage<Id>().size() == count, "duplicate: a node a prefab made, and no entity, are refused");
			check(generated != entt::null && !editor.rename_entity(generated, "x"), "rename: a node a prefab made is refused");

			check(editor.rename_entity(a, "Lamp") && registry.get<Name>(a).Value == "Lamp", "rename: an entity takes its new name");
			editor.set_playing(true);
			check(editor.duplicate_entity(a) == entt::null && !editor.rename_entity(a, "Other") && registry.get<Name>(a).Value == "Lamp" && registry.storage<Id>().size() == count, "duplicate and rename: refused while the scene plays");
			editor.set_playing(false);
		}

		void check_scene_editor(Check& check, AssetPipelineModule& pipeline)
		{
			register_scene_components();
			SmokeAssets assets(pipeline);
			AssetDatabase& database = pipeline.get_database();
			AssetManager& manager = *Engine::get().get_module<AssetManager>();
			SceneModule& module = *Engine::get().get_module<SceneModule>();

			const std::optional<AssetRef> prefabRef = database.ensure_imported(assets.write("editor_model.gltf", test_model_gltf()));
			check(prefabRef.has_value(), "the model for the editor checks imports");
			if (!prefabRef)
			{
				return;
			}

			SceneEditor editor(manager, module);
			editor.set_scene(CreateRef<Scene>(), {});
			Scene& scene = editor.scene();
			entt::registry& registry = scene.registry();
			auto pump = [&](SceneEditor& target)
			{
				for (u32 i = 0; i < 2000 && (target.has_pending() || target.is_opening()); i++)
				{
					target.update();
					std::this_thread::sleep_for(std::chrono::milliseconds(2));
				}
			};

			// structure
			check(!editor.dirty(), "a scene that was just set is not dirty");
			const entt::entity a = editor.create_entity(entt::null);
			const entt::entity b = editor.create_entity(a);
			const entt::entity c = editor.create_entity(b);
			check(editor.dirty() && registry.get<Hierarchy>(c).Parent == b && registry.get<Hierarchy>(b).Parent == a, "entities can be created under each other, and that makes the scene dirty");
			check(!editor.reparent(a, c) && registry.get<Hierarchy>(b).Parent == a && registry.get<Hierarchy>(a).Parent == entt::null, "a reparent that would make a loop is refused and changes nothing");
			check(editor.reparent(c, a) && registry.get<Hierarchy>(c).Parent == a, "an entity can move to another parent");
			check(editor.delete_entity(b) && !registry.valid(b) && registry.valid(c), "deleting an entity leaves its siblings");

			// a prefab instance
			const entt::entity instance = editor.add_prefab(*prefabRef, entt::null);
			check(instance != entt::null && registry.all_of<PrefabInstance>(instance), "a prefab can be added");
			pump(editor);
			check(registry.view<PrefabChild>().size() == 4, "the nodes of the prefab appear once it has loaded (found " + std::to_string(registry.view<PrefabChild>().size()) + ")");

			auto find_child = [&](const std::string& name)
			{
				for (const entt::entity entity : registry.view<PrefabChild>())
				{
					if (registry.get<Name>(entity).Value == name)
					{
						return entity;
					}
				}
				return entt::entity{ entt::null };
			};

			const entt::entity door = find_child("Door");
			check(door != entt::null, "the door is there");
			if (door == entt::null)
			{
				return;
			}

			const u64 doorNode = registry.get<PrefabChild>(door).NodeId;
			check(editor.set_field(door, "Transform", "Position", FieldValue(glm::vec3(4, 5, 6))), "a field of a prefab node can be set");
			const std::vector<PrefabOverride>& overrides = registry.get<PrefabInstance>(instance).Overrides;
			check(overrides.size() == 1 && overrides[0].Kind == OverrideKind::SetField && overrides[0].NodeId == doorNode && overrides[0].Component == "Transform" && overrides[0].Field == "Position", "that is recorded as one override on the instance");
			check(registry.get<Transform>(door).Position == glm::vec3(4, 5, 6) && editor.is_overridden(door, "Transform", "Position") && !editor.is_overridden(door, "Transform", "Scale"), "the node shows the new value and is marked");
			check(!editor.set_field(door, "Transform", "Position", FieldValue(1.0f)) && editor.set_field(door, "Transform", "Position", FieldValue(glm::vec3(7, 7, 7))) && registry.get<PrefabInstance>(instance).Overrides.size() == 1, "a wrong value is refused, and setting the same field again changes the one override");

			const SceneData saved = editor.to_scene_data();
			bool savesGenerated = false;
			for (const EntityRecord& record : saved.Entities)
			{
				savesGenerated = savesGenerated || record.Id == registry.get<Id>(door).Guid;
			}
			check(saved.Entities.size() == 3 && !savesGenerated, "the entities of an instance are not saved, the instance is");

			check(editor.revert_field(door, "Transform", "Position") && registry.get<PrefabInstance>(instance).Overrides.empty() && registry.get<Transform>(door).Position == glm::vec3(0, 0, 0) && !editor.is_overridden(door, "Transform", "Position"), "reverting a field removes the override and brings the prefab's value back");

			check(editor.set_field(door, "Transform", "Scale", FieldValue(glm::vec3(2, 2, 2))), "set a field to keep an override for the save below");

			const entt::entity wheel = find_child("Wheel");
			check(wheel != entt::null && editor.delete_entity(wheel) && !registry.valid(wheel) && registry.view<PrefabChild>().size() == 3, "deleting a prefab node removes it");
			const std::vector<PrefabOverride>& afterDelete = registry.get<PrefabInstance>(instance).Overrides;
			check(afterDelete.size() == 2 && afterDelete[1].Kind == OverrideKind::RemoveNode, "that is recorded as a RemoveNode override");

			const entt::entity bolt = editor.create_entity(door);
			check(bolt != entt::null && registry.all_of<PrefabChild>(bolt) && registry.get<PrefabChild>(bolt).NodeId == ADDED_CHILD_NODE && registry.get<PrefabInstance>(instance).Overrides.back().Kind == OverrideKind::AddChild, "an entity created under a prefab node is an AddChild override");
			check(bolt != entt::null && editor.set_field(bolt, "Name", "Value", FieldValue(std::string("Bolt"))), "an added child can be renamed");
			check(!editor.reparent(bolt, entt::null) && !editor.reparent(a, door), "entities of an instance cannot be moved out, and nothing can be moved into one");

			// the scene's look
			{
				SceneEditor looks(manager, module);
				looks.set_scene(CreateRef<Scene>(), {});
				check(!looks.dirty(), "a fresh scene is clean");
				looks.edit_settings(looks.settings());
				check(!looks.dirty(), "settings_edit_marks_dirty: applying the same settings does not make the scene dirty");
				looks.edit_settings(smoke_settings());
				check(looks.dirty() && looks.settings() == sanitized(smoke_settings()), "settings_edit_marks_dirty: changed settings make it dirty and are kept (sanitized)");
			}
			SceneData beforeLook = editor.to_scene_data();
			beforeLook.Settings = SceneSettings{};
			const std::vector<std::byte> entitiesBefore = serialize(beforeLook);
			editor.edit_settings(smoke_settings());
			SceneData afterLook = editor.to_scene_data();
			check(afterLook.Settings == sanitized(smoke_settings()), "the edited settings are what is saved");
			afterLook.Settings = SceneSettings{};
			check(serialize(afterLook) == entitiesBefore, "settings_prefab_untouched: editing the settings leaves every entity and prefab override as it was");

			// save, open
			const std::string relativePath = "assets/_scene_smoke/edited.wscene";
			check(editor.save_as(relativePath) && !editor.dirty(), "the scene saves, and is clean afterwards");
			const std::optional<AssetRef> editedRef = database.ensure_imported(assets.folder() / "edited.wscene");
			check(editedRef.has_value(), "the saved file imports");
			if (!editedRef)
			{
				return;
			}

			SceneEditor reopened(manager, module);
			reopened.open(relativePath);
			check(reopened.is_opening(), "opening starts a load");
			pump(reopened);
			check(!reopened.is_opening() && reopened.get_scene() != nullptr, "the scene opens");
			if (reopened.get_scene() == nullptr)
			{
				return;
			}

			check(guids_of(reopened.scene()) == guids_of(scene), "the opened scene has the same entities, ids included");
			const bool sameSave = serialize(reopened.to_scene_data()) == serialize(editor.to_scene_data());
			check(sameSave, "saving what was opened gives the file again");
			if (!sameSave)
			{
				WI_SCENE_LOG(Error, "edited:   {0}", scene_to_json(editor.to_scene_data()).dump());
				WI_SCENE_LOG(Error, "reopened: {0}", scene_to_json(reopened.to_scene_data()).dump());
			}
			check(!reopened.dirty() && reopened.path() == relativePath, "an opened scene is clean and knows its file");
			check(reopened.settings() == sanitized(smoke_settings()), "settings_save_open_roundtrip: the settings come back from the saved file");
			const entt::entity reopenedDoor = reopened.scene().find(registry.get<Id>(door).Guid);
			check(reopenedDoor != entt::null && reopened.scene().registry().get<Transform>(reopenedDoor).Position == glm::vec3(0, 0, 0) && reopened.scene().registry().get<Transform>(reopenedDoor).Scale == glm::vec3(2, 2, 2), "the overrides are applied in the opened scene");
			check(reopenedDoor != entt::null && reopened.set_field(reopenedDoor, "Transform", "Position", FieldValue(glm::vec3(1, 1, 1))) && reopened.dirty(), "editing an opened scene makes it dirty");

			// unpack
			entt::entity reopenedInstance = entt::null;
			for (const entt::entity entity : reopened.scene().registry().view<PrefabInstance>())
			{
				reopenedInstance = entity;
			}
			const size_t entityCount = entity_count(reopened.get_scene());
			check(reopenedInstance != entt::null && reopened.unpack(reopenedInstance), "an instance can be unpacked");
			check(reopenedInstance != entt::null && !reopened.scene().registry().all_of<PrefabInstance>(reopenedInstance) && reopened.scene().registry().view<PrefabChild>().empty() && entity_count(reopened.get_scene()) == entityCount, "unpacking leaves plain entities and no link to the prefab");
			check(reopened.to_scene_data().Entities.size() == entityCount, "after unpacking, every entity is saved");
		}
	}

	namespace
	{
		SceneSettings physics_smoke_settings()
		{
			SceneSettings s;
			s.Physics.Gravity = glm::vec3(0.0f, -3.7f, 1.0f);
			s.Physics.FixedRateHz = 90;
			s.Physics.MaxStepsPerWake = 6;
			s.Physics.VelocityIterations = 12;
			s.Physics.PositionIterations = 3;
			s.Physics.SleepThreshold = 0.05f;
			return s;
		}

		// RigidBody, Collider and the scene's physics settings survive save, cook and load (binary and the text of the file),
		// and a prefab override of Collider.Size applies.
		void check_physics_data(Check& check)
		{
			register_scene_components();
			const ComponentRegistry& components = ComponentRegistry::get();
			check(components.find("RigidBody") != nullptr && components.find("Collider") != nullptr, "physics: RigidBody and Collider are registered");

			const AssetRef material{ AssetGuid::generate(), 0 };
			const AssetRef meshRef{ AssetGuid::generate(), 1 };

			Scene source;
			source.set_settings(physics_smoke_settings());
			const EntityGuid bodyId = EntityGuid::generate();
			const entt::entity body = source.create(bodyId, "Body");
			RigidBody rb;
			rb.Motion = MotionType::Kinematic;
			rb.MassOverride = 12.5f;
			rb.LinearDamping = 0.3f;
			rb.AngularDamping = 0.4f;
			rb.GravityFactor = 0.5f;
			rb.ContinuousCollision = true;
			rb.AllowSleeping = false;
			rb.ReportContacts = true;
			rb.LockPositionY = true;
			rb.LockRotationX = true;
			rb.LockRotationZ = true;
			source.registry().emplace<RigidBody>(body, rb);
			Collider col;
			col.Shape = ColliderShape::Capsule;
			col.Size = glm::vec3(0.25f, 1.5f, 0.75f);
			col.Offset = glm::vec3(0.0f, 0.5f, 0.0f);
			col.RotationEuler = glm::vec3(10.0f, 20.0f, 30.0f);
			col.IsTrigger = true;
			col.Layer = 5;
			col.Material = material;
			col.Mesh = meshRef;
			source.registry().emplace<Collider>(body, col);

			const SceneData first = data_from_scene(source, "physics");
			check(first.Settings.Physics == physics_smoke_settings().Physics, "physics: the scene's physics settings are in the records");
			const std::vector<std::byte> bytes = serialize(first);
			std::optional<SceneData> back = deserialize<SceneData>(bytes);
			check(back.has_value() && back->Settings.Physics == physics_smoke_settings().Physics, "physics: the settings survive the cooked format");
			if (!back)
			{
				return;
			}

			Scene copy;
			const ApplyReport report = apply_scene_data(copy, *back);
			check(report.UnknownComponents == 0 && report.UnknownFields == 0, "physics: nothing in the saved scene is unknown");
			check(copy.settings().Physics == physics_smoke_settings().Physics, "physics: the loaded scene has the physics settings");
			const entt::entity loaded = copy.find(bodyId);
			check(loaded != entt::null && copy.registry().all_of<RigidBody, Collider>(loaded), "physics: the entity keeps its RigidBody and Collider");
			if (loaded != entt::null && copy.registry().all_of<RigidBody, Collider>(loaded))
			{
				const RigidBody& r = copy.registry().get<RigidBody>(loaded);
				check(r.Motion == MotionType::Kinematic && r.MassOverride == 12.5f && r.LinearDamping == 0.3f && r.AngularDamping == 0.4f && r.GravityFactor == 0.5f, "physics: RigidBody numbers survive");
				check(r.ContinuousCollision && !r.AllowSleeping && r.ReportContacts, "physics: RigidBody flags survive");
				check(!r.LockPositionX && r.LockPositionY && !r.LockPositionZ && r.LockRotationX && !r.LockRotationY && r.LockRotationZ, "physics: RigidBody locks survive");
				const Collider& c = copy.registry().get<Collider>(loaded);
				check(c.Shape == ColliderShape::Capsule && c.Size == col.Size && c.Offset == col.Offset && c.RotationEuler == col.RotationEuler, "physics: Collider shape and geometry survive");
				check(c.IsTrigger && c.Layer == 5, "physics: Collider.IsTrigger and Layer survive");
				check(c.Material == material && c.Mesh == meshRef, "physics: Collider asset references survive");
			}
			check(serialize(data_from_scene(copy, "physics")) == bytes, "physics: save, load, save gives the same bytes");

			// the text of the file
			const SceneJsonResult viaJson = scene_from_json(SceneJson::parse(scene_to_json(first).dump(2)));
			check(viaJson.Scene.has_value() && viaJson.Scene->Settings.Physics == physics_smoke_settings().Physics, "physics: the settings survive the text of the file (" + viaJson.Error + ")");
			check(viaJson.Scene.has_value() && serialize(*viaJson.Scene) == bytes, "physics: the text of the file gives the same binary scene (" + viaJson.Error + ")");

			// a prefab override of Collider.Size
			PrefabData prefab;
			prefab.Name = "crate";
			PrefabNode node = make_node("Crate", -1);
			node.Components.push_back(ComponentRecord{ "Collider", { FieldRecord{ "Size", FieldValue(glm::vec3(0.5f)) } } });
			prefab.Nodes = { node };
			assign_stable_ids(prefab.Nodes);
			const u64 nodeId = prefab.Nodes[0].StableId;
			EntityRecord instance = make_instance(EntityGuid::generate(), AssetRef{ AssetGuid::generate(), 0 });
			instance.Overrides = { set_field_override(nodeId, "Collider", "Size", FieldValue(glm::vec3(2, 3, 4))) };
			const ExpandResult expanded = expand_prefab(instance, prefab);
			const FieldValue* size = expanded.Entities.size() == 2 ? find_field(expanded.Entities[1].Components, "Collider", "Size") : nullptr;
			check(size != nullptr && std::get_if<glm::vec3>(size) != nullptr && *std::get_if<glm::vec3>(size) == glm::vec3(2, 3, 4), "physics: a prefab override of Collider.Size applies");

			// ... and lands on the component of the loaded entity
			SceneData withInstance;
			withInstance.Name = "crate_scene";
			withInstance.Entities = expanded.Entities;
			Scene crateScene;
			apply_scene_data(crateScene, withInstance);
			const entt::entity crate = expanded.Entities.size() == 2 ? crateScene.find(expanded.Entities[1].Id) : entt::null;
			check(crate != entt::null && crateScene.registry().all_of<Collider>(crate) && crateScene.registry().get<Collider>(crate).Size == glm::vec3(2, 3, 4), "physics: the overridden Collider.Size reaches the loaded component");
		}

		// Joint survives save, cook, load (binary and the text of the file) and a prefab override of LimitMax applies.
		void check_joint_data(Check& check)
		{
			register_scene_components();
			check(ComponentRegistry::get().find("Joint") != nullptr, "joint: Joint is registered");

			Scene source;
			const EntityGuid otherId = EntityGuid::generate();
			source.create(otherId, "Other");
			const EntityGuid ownerId = EntityGuid::generate();
			const entt::entity owner = source.create(ownerId, "Owner");
			Joint joint;
			joint.Type = JointType::Slider;
			joint.Connected = otherId;
			joint.CollideConnected = true;
			joint.AnchorA = glm::vec3(1, 2, 3);
			joint.AnchorB = glm::vec3(-1, -2, -3);
			joint.Axis = glm::vec3(0, 0, 1);
			joint.NormalAxis = glm::vec3(0, 1, 0);
			joint.LimitMin = -1.5f;
			joint.LimitMax = 2.5f;
			joint.SwingHalfAngle = 33.0f;
			joint.TwistHalfAngle = 22.0f;
			joint.SpringFrequency = 4.0f;
			joint.SpringDamping = 0.5f;
			joint.MotorMode = JointMotorMode::Velocity;
			joint.MotorTarget = 1.25f;
			joint.MotorMaxForce = 99.0f;
			joint.BreakForce = 123.0f;
			joint.BreakTorque = 45.0f;
			source.registry().emplace<Joint>(owner, joint);

			const SceneData first = data_from_scene(source, "joint");
			const std::vector<std::byte> bytes = serialize(first);
			std::optional<SceneData> back = deserialize<SceneData>(bytes);
			check(back.has_value(), "joint: the scene survives the cooked format");
			if (!back)
			{
				return;
			}
			Scene copy;
			const ApplyReport report = apply_scene_data(copy, *back);
			check(report.UnknownComponents == 0 && report.UnknownFields == 0, "joint: nothing in the saved scene is unknown");
			const entt::entity loaded = copy.find(ownerId);
			check(loaded != entt::null && copy.registry().all_of<Joint>(loaded), "joint: the entity keeps its Joint");
			if (loaded != entt::null && copy.registry().all_of<Joint>(loaded))
			{
				const Joint& j = copy.registry().get<Joint>(loaded);
				check(j.Type == JointType::Slider && j.Connected == otherId && j.CollideConnected, "joint: type, connection and collide flag survive");
				check(j.AnchorA == joint.AnchorA && j.AnchorB == joint.AnchorB && j.Axis == joint.Axis && j.NormalAxis == joint.NormalAxis, "joint: anchors and axes survive");
				check(j.LimitMin == -1.5f && j.LimitMax == 2.5f && j.SwingHalfAngle == 33.0f && j.TwistHalfAngle == 22.0f, "joint: limits survive");
				check(j.SpringFrequency == 4.0f && j.SpringDamping == 0.5f, "joint: spring survives");
				check(j.MotorMode == JointMotorMode::Velocity && j.MotorTarget == 1.25f && j.MotorMaxForce == 99.0f, "joint: motor survives");
				check(j.BreakForce == 123.0f && j.BreakTorque == 45.0f, "joint: break limits survive");
			}
			check(serialize(data_from_scene(copy, "joint")) == bytes, "joint: save, load, save gives the same bytes");

			const SceneJsonResult viaJson = scene_from_json(SceneJson::parse(scene_to_json(first).dump(2)));
			check(viaJson.Scene.has_value() && serialize(*viaJson.Scene) == bytes, "joint: the text of the file gives the same binary scene (" + viaJson.Error + ")");

			PrefabData prefab;
			prefab.Name = "door";
			PrefabNode node = make_node("Door", -1);
			node.Components.push_back(ComponentRecord{ "Joint", { FieldRecord{ "LimitMax", FieldValue(90.0f) } } });
			prefab.Nodes = { node };
			assign_stable_ids(prefab.Nodes);
			const u64 nodeId = prefab.Nodes[0].StableId;
			EntityRecord instance = make_instance(EntityGuid::generate(), AssetRef{ AssetGuid::generate(), 0 });
			instance.Overrides = { set_field_override(nodeId, "Joint", "LimitMax", FieldValue(45.0f)) };
			const ExpandResult expanded = expand_prefab(instance, prefab);
			const FieldValue* limit = expanded.Entities.size() == 2 ? find_field(expanded.Entities[1].Components, "Joint", "LimitMax") : nullptr;
			check(limit != nullptr && std::get_if<float>(limit) != nullptr && *std::get_if<float>(limit) == 45.0f, "joint: a prefab override of Joint.LimitMax applies");
		}

		// Decal survives save, cook, load (binary and the text of the file), a scene clone and a prefab override of Opacity; a scene file
		// from before ReceivesDecals loads with it true.
		void check_decal_data(Check& check)
		{
			register_scene_components();
			check(ComponentRegistry::get().find("Decal") != nullptr, "decal: Decal is registered");

			Scene source;
			const EntityGuid ownerId = EntityGuid::generate();
			const entt::entity owner = source.create(ownerId, "Decal");
			Decal decal;
			decal.Material = AssetRef{ AssetGuid::generate(), 2 };
			decal.Size = glm::vec3(2, 3, 0.5f);
			decal.Opacity = 0.75f;
			decal.AffectColor = false;
			decal.AffectNormal = false;
			decal.AffectOrm = false;
			decal.AffectEmissive = false;
			decal.NormalAngleFadeStart = 30.0f;
			decal.NormalAngleFadeEnd = 45.0f;
			decal.EdgeFade = 0.25f;
			decal.SortOrder = 7;
			decal.FadeInTime = 0.5f;
			decal.Lifetime = 4.0f;
			decal.FadeOutTime = 1.5f;
			source.registry().emplace<Decal>(owner, decal);
			const EntityGuid meshId = EntityGuid::generate();
			const entt::entity meshEntity = source.create(meshId, "Wall");
			source.registry().emplace<MeshRenderer>(meshEntity, MeshRenderer{ AssetRef{}, {}, false });

			const auto same = [](const Decal& a, const Decal& b)
			{
				return a.Material == b.Material && a.Size == b.Size && a.Opacity == b.Opacity && a.AffectColor == b.AffectColor
					&& a.AffectNormal == b.AffectNormal && a.AffectOrm == b.AffectOrm && a.AffectEmissive == b.AffectEmissive
					&& a.NormalAngleFadeStart == b.NormalAngleFadeStart && a.NormalAngleFadeEnd == b.NormalAngleFadeEnd
					&& a.EdgeFade == b.EdgeFade && a.SortOrder == b.SortOrder && a.FadeInTime == b.FadeInTime
					&& a.Lifetime == b.Lifetime && a.FadeOutTime == b.FadeOutTime;
			};

			const SceneData first = data_from_scene(source, "decal");
			const std::vector<std::byte> bytes = serialize(first);
			std::optional<SceneData> back = deserialize<SceneData>(bytes);
			check(back.has_value(), "decal: the scene survives the cooked format");
			if (!back)
			{
				return;
			}
			Scene copy;
			const ApplyReport report = apply_scene_data(copy, *back);
			check(report.UnknownComponents == 0 && report.UnknownFields == 0, "decal: nothing in the saved scene is unknown");
			const entt::entity loaded = copy.find(ownerId);
			check(loaded != entt::null && copy.registry().all_of<Decal>(loaded) && same(copy.registry().get<Decal>(loaded), decal), "decal: every field survives save and load");
			const entt::entity loadedMesh = copy.find(meshId);
			check(loadedMesh != entt::null && copy.registry().all_of<MeshRenderer>(loadedMesh) && !copy.registry().get<MeshRenderer>(loadedMesh).ReceivesDecals, "decal: MeshRenderer.ReceivesDecals false survives");
			check(serialize(data_from_scene(copy, "decal")) == bytes, "decal: save, load, save gives the same bytes");

			const SceneJsonResult viaJson = scene_from_json(SceneJson::parse(scene_to_json(first).dump(2)));
			check(viaJson.Scene.has_value() && serialize(*viaJson.Scene) == bytes, "decal: the text of the file gives the same binary scene (" + viaJson.Error + ")");

			Ref<Scene> cloned = clone_scene(source);
			const entt::entity clonedDecal = cloned->find(ownerId);
			check(clonedDecal != entt::null && cloned->registry().all_of<Decal>(clonedDecal) && same(cloned->registry().get<Decal>(clonedDecal), decal), "decal: a scene clone keeps every field");

			// A file written before ReceivesDecals existed has no such field: the mesh must receive decals.
			SceneData old = first;
			for (EntityRecord& entity : old.Entities)
			{
				for (ComponentRecord& component : entity.Components)
				{
					if (component.Name == "MeshRenderer")
					{
						component.Fields.erase(std::remove_if(component.Fields.begin(), component.Fields.end(), [](const FieldRecord& f) { return f.Name == "ReceivesDecals"; }), component.Fields.end());
					}
				}
			}
			Scene oldScene;
			apply_scene_data(oldScene, old);
			const entt::entity oldMesh = oldScene.find(meshId);
			check(oldMesh != entt::null && oldScene.registry().all_of<MeshRenderer>(oldMesh) && oldScene.registry().get<MeshRenderer>(oldMesh).ReceivesDecals, "decal: a scene without ReceivesDecals loads with it true");

			PrefabData prefab;
			prefab.Name = "stain";
			PrefabNode node = make_node("Stain", -1);
			node.Components.push_back(ComponentRecord{ "Decal", { FieldRecord{ "Opacity", FieldValue(1.0f) } } });
			prefab.Nodes = { node };
			assign_stable_ids(prefab.Nodes);
			const u64 nodeId = prefab.Nodes[0].StableId;
			EntityRecord instance = make_instance(EntityGuid::generate(), AssetRef{ AssetGuid::generate(), 0 });
			instance.Overrides = { set_field_override(nodeId, "Decal", "Opacity", FieldValue(0.5f)) };
			const ExpandResult expanded = expand_prefab(instance, prefab);
			const FieldValue* opacity = expanded.Entities.size() == 2 ? find_field(expanded.Entities[1].Components, "Decal", "Opacity") : nullptr;
			check(opacity != nullptr && std::get_if<float>(opacity) != nullptr && *std::get_if<float>(opacity) == 0.5f, "decal: a prefab override of Decal.Opacity applies");
		}

		// The fades and the expiry of a decal (decal_lifetime.h): in, hold for Lifetime, out, then gone; permanent without a Lifetime.
		void check_decal_lifetime(Check& check)
		{
			register_scene_components();

			Decal timed;
			timed.FadeInTime = 0.2f;
			timed.Lifetime = 1.0f;
			timed.FadeOutTime = 0.2f;
			const auto approx = [](float a, float b) { return std::abs(a - b) < 1e-4f; };
			check(approx(decal_fade(timed, 0.0f), 0.0f) && approx(decal_fade(timed, 0.1f), 0.5f) && approx(decal_fade(timed, 0.2f), 1.0f), "decal_lifetime: it fades in over FadeInTime");
			check(approx(decal_fade(timed, 0.7f), 1.0f) && approx(decal_fade(timed, 1.2f), 1.0f), "decal_lifetime: it holds full opacity for Lifetime");
			check(approx(decal_fade(timed, 1.3f), 0.5f) && approx(decal_fade(timed, 1.4f), 0.0f), "decal_lifetime: it fades out over FadeOutTime");
			check(!decal_expired(timed, 1.39f) && decal_expired(timed, 1.41f), "decal_lifetime: it expires after FadeInTime + Lifetime + FadeOutTime");

			Decal cut;
			cut.Lifetime = 1.0f; // no fades: gone at once when the time is up
			check(approx(decal_fade(cut, 0.99f), 1.0f) && approx(decal_fade(cut, 1.0f), 0.0f) && decal_expired(cut, 1.0f), "decal_lifetime: without fades it just ends");

			Decal permanent;
			check(approx(decal_fade(permanent, 1e6f), 1.0f) && !decal_expired(permanent, 1e6f), "decal_lifetime: a decal without a Lifetime never expires");

			Scene scene;
			const entt::entity doomed = scene.create("doomed");
			scene.registry().emplace<Decal>(doomed, timed);
			const entt::entity staying = scene.create("staying");
			scene.registry().emplace<Decal>(staying, permanent);
			Decal fadingIn = permanent;
			fadingIn.FadeInTime = 0.5f;
			const entt::entity rising = scene.create("rising");
			scene.registry().emplace<Decal>(rising, fadingIn);

			for (int step = 0; step < 13; step++)
			{
				tick_decals(scene, 0.1f);
			}
			check(scene.registry().valid(doomed) && scene.registry().all_of<DecalAge>(doomed), "decal_lifetime: a decal is still there before its time is up");
			check(approx(scene.registry().get<DecalAge>(doomed).Seconds, 1.3f), "decal_lifetime: its age is the game time that passed");
			check(!scene.registry().all_of<DecalAge>(staying), "decal_lifetime: a permanent decal that shows at once is not aged");
			tick_decals(scene, 0.0f);
			tick_decals(scene, -1.0f);
			tick_decals(scene, std::numeric_limits<float>::quiet_NaN());
			check(approx(scene.registry().get<DecalAge>(doomed).Seconds, 1.3f), "decal_lifetime: a step of 0, a negative one or NaN ages nothing");
			tick_decals(scene, 0.1f);
			check(!scene.registry().valid(doomed), "decal_lifetime: the expired decal is destroyed");
			check(scene.registry().valid(staying) && scene.registry().valid(rising), "decal_lifetime: the others stay");
		}

		// ParticleSystem survives save, cook, load (binary and text), a scene clone and a prefab override, and its runtime is never saved.
		void check_particle_system_data(Check& check)
		{
			register_scene_components();
			check(ComponentRegistry::get().find("ParticleSystem") != nullptr, "particles: ParticleSystem is registered");

			Scene source;
			const EntityGuid ownerId = EntityGuid::generate();
			const entt::entity owner = source.create(ownerId, "Fire");
			ParticleSystem system;
			system.Effect = AssetRef{ AssetGuid::generate(), 4 };
			system.PlayOnStart = false;
			system.Looping = false;
			system.TimeScale = 2.0f;
			system.SortBias = 3;
			system.ColorTint = glm::vec4(0.5f, 0.25f, 0.75f, 1.0f);
			system.SizeScale = 2.0f;
			system.RateScale = 0.5f;
			source.registry().emplace<ParticleSystem>(owner, system);
			source.registry().emplace<ParticleRuntime>(owner, ParticleRuntime{ true, true });

			const auto same = [](const ParticleSystem& a, const ParticleSystem& b)
			{
				return a.Effect == b.Effect && a.PlayOnStart == b.PlayOnStart && a.Looping == b.Looping && a.TimeScale == b.TimeScale && a.SortBias == b.SortBias
					&& a.ColorTint == b.ColorTint && a.SizeScale == b.SizeScale && a.RateScale == b.RateScale;
			};

			const SceneData first = data_from_scene(source, "particles");
			const std::vector<std::byte> bytes = serialize(first);
			std::optional<SceneData> back = deserialize<SceneData>(bytes);
			check(back.has_value(), "particles: the scene survives the cooked format");
			if (!back)
			{
				return;
			}
			Scene copy;
			const ApplyReport report = apply_scene_data(copy, *back);
			check(report.UnknownComponents == 0 && report.UnknownFields == 0, "particles: nothing in the saved scene is unknown");
			const entt::entity loaded = copy.find(ownerId);
			check(loaded != entt::null && copy.registry().all_of<ParticleSystem>(loaded) && same(copy.registry().get<ParticleSystem>(loaded), system), "particles: every field survives save and load");
			check(loaded != entt::null && !copy.registry().all_of<ParticleRuntime>(loaded), "particles: the runtime state is not saved");
			check(serialize(data_from_scene(copy, "particles")) == bytes, "particles: save, load, save gives the same bytes");

			const SceneJsonResult viaJson = scene_from_json(SceneJson::parse(scene_to_json(first).dump(2)));
			check(viaJson.Scene.has_value() && serialize(*viaJson.Scene) == bytes, "particles: the text of the file gives the same binary scene (" + viaJson.Error + ")");

			Ref<Scene> cloned = clone_scene(source);
			const entt::entity clonedSystem = cloned->find(ownerId);
			check(clonedSystem != entt::null && cloned->registry().all_of<ParticleSystem>(clonedSystem) && same(cloned->registry().get<ParticleSystem>(clonedSystem), system), "particles: a scene clone keeps every field");
			check(clonedSystem != entt::null && !cloned->registry().all_of<ParticleRuntime>(clonedSystem), "particles: a clone starts without runtime state");

			PrefabData prefab;
			prefab.Name = "fire";
			PrefabNode node = make_node("Fire", -1);
			node.Components.push_back(ComponentRecord{ "ParticleSystem", { FieldRecord{ "TimeScale", FieldValue(1.0f) } } });
			prefab.Nodes = { node };
			assign_stable_ids(prefab.Nodes);
			const u64 nodeId = prefab.Nodes[0].StableId;
			EntityRecord instance = make_instance(EntityGuid::generate(), AssetRef{ AssetGuid::generate(), 0 });
			instance.Overrides = { set_field_override(nodeId, "ParticleSystem", "TimeScale", FieldValue(0.25f)) };
			const ExpandResult expanded = expand_prefab(instance, prefab);
			const FieldValue* scale = expanded.Entities.size() == 2 ? find_field(expanded.Entities[1].Components, "ParticleSystem", "TimeScale") : nullptr;
			check(scale != nullptr && std::get_if<float>(scale) != nullptr && *std::get_if<float>(scale) == 0.25f, "particles: a prefab override of ParticleSystem.TimeScale applies");
		}

		// What the tick makes of an effect: how many particles each emitter spawns, from the rate, the bursts, the loop, the time scale and the step.
		void check_particle_ticks(Check& check)
		{
			register_scene_components();

			ParticleEffectData effect;
			ParticleEmitterData emitter;
			emitter.Name = "E";
			emitter.Looping = false;
			emitter.Duration = 5.0f;
			emitter.Rate = ParticleValue::constant(10.0f);
			emitter.Lifetime = ParticleValue::constant(1.0f);
			emitter.bake();
			effect.Emitters = { emitter };
			const ParticleEffectLookup lookup = [&](const AssetRef&) -> const ParticleEffectData* { return &effect; };
			const AssetRef reference{ AssetGuid::generate(), 0 };

			// Runs a scene with one system for `seconds` at `hz` and returns what its first emitter spawned.
			const auto spawned = [&](const ParticleSystem& system, float seconds, float hz, const ParticleEffectLookup& effects) -> u32
			{
				Scene scene;
				const entt::entity entity = scene.create("fx");
				ParticleSystem copy = system;
				copy.Effect = reference;
				scene.registry().emplace<ParticleSystem>(entity, copy);
				u32 total = 0;
				for (int frame = 0; frame < static_cast<int>(seconds * hz); frame++)
				{
					tick_particle_systems(scene, 1.0f / hz, effects);
					if (const ParticleRuntime* runtime = scene.registry().try_get<ParticleRuntime>(entity))
					{
						total += runtime->SpawnCount[0];
					}
				}
				return total;
			};
			const auto within = [](u32 value, u32 expected, u32 slack) { return value + slack >= expected && value <= expected + slack; };

			ParticleSystem plain;
			check(within(spawned(plain, 7.0f, 60.0f, lookup), 50, 1), "particle_ticks: a rate of 10 for the 5 s duration spawns 50 and then stops");
			check(within(spawned(plain, 7.0f, 30.0f, lookup), 50, 1), "particle_ticks: the count does not depend on the frame rate (the fraction is carried)");

			ParticleSystem scaled;
			scaled.RateScale = 0.5f;
			check(within(spawned(scaled, 7.0f, 60.0f, lookup), 25, 1), "particle_ticks: RateScale scales the rate");
			ParticleSystem fast;
			fast.TimeScale = 2.0f;
			check(within(spawned(fast, 7.0f, 60.0f, lookup), 50, 1), "particle_ticks: TimeScale 2 plays the same emission in half the time");

			// the project's spawn rate scale (quality), on top of the system's own
			const auto spawned_with_quality = [&](float qualityScale) -> u32
			{
				Scene scene;
				const entt::entity entity = scene.create("fx");
				ParticleSystem system;
				system.Effect = reference;
				scene.registry().emplace<ParticleSystem>(entity, system);
				u32 total = 0;
				for (int frame = 0; frame < 7 * 60; frame++)
				{
					tick_particle_systems(scene, 1.0f / 60.0f, lookup, false, qualityScale);
					total += scene.registry().get<ParticleRuntime>(entity).SpawnCount[0];
				}
				return total;
			};
			check(within(spawned_with_quality(0.5f), 25, 1), "particle_ticks: the project's spawn rate scale multiplies the rate");
			check(spawned_with_quality(0.0f) == 0, "particle_ticks: a spawn rate scale of 0 spawns nothing");
			check(within(spawned_with_quality(2.0f), 100, 2), "particle_ticks: a spawn rate scale of 2 doubles it");

			// edit mode: only the systems that are being previewed advance
			{
				Scene scene;
				const entt::entity idle = scene.create("idle");
				const entt::entity previewed = scene.create("previewed");
				ParticleSystem system;
				system.Effect = reference;
				scene.registry().emplace<ParticleSystem>(idle, system);
				scene.registry().emplace<ParticleSystem>(previewed, system);
				particle_runtime_of(scene.registry(), previewed).Preview = true;
				for (int frame = 0; frame < 30; frame++)
				{
					tick_particle_systems(scene, 1.0f / 60.0f, lookup, true);
				}
				check(scene.registry().get<ParticleRuntime>(previewed).Time > 0.45f, "particle_ticks: a previewed system advances in edit mode");
				const ParticleRuntime* untouched = scene.registry().try_get<ParticleRuntime>(idle);
				check(untouched == nullptr || untouched->Time == 0.0f, "particle_ticks: a system that is not previewed does not (the control)");
			}

			ParticleSystem paused;
			paused.TimeScale = 0.0f;
			check(spawned(paused, 3.0f, 60.0f, lookup) == 0, "particle_ticks: TimeScale 0 pauses");
			ParticleSystem idle;
			idle.PlayOnStart = false;
			check(spawned(idle, 3.0f, 60.0f, lookup) == 0, "particle_ticks: without PlayOnStart nothing plays");

			// bursts: once in a non-looping emission, once per cycle in a looping one
			effect.Emitters[0].Bursts = { ParticleBurst{ 1.0f, 20 }, ParticleBurst{ 0.0f, 3 } };
			check(within(spawned(plain, 7.0f, 60.0f, lookup), 50 + 23, 1), "particle_ticks: bursts fire once, also the one at time 0");
			effect.Emitters[0].Looping = true;
			effect.Emitters[0].Duration = 2.0f;
			effect.Emitters[0].Rate = ParticleValue::constant(0.0f);
			check(within(spawned(plain, 6.5f, 60.0f, lookup), 3 * 23 + 3, 0), "particle_ticks: a looping emitter fires its bursts again every cycle");
			ParticleSystem once;
			once.Looping = false;
			check(within(spawned(once, 6.5f, 60.0f, lookup), 23, 0), "particle_ticks: Looping off on the system plays one pass of a looping emitter");

			effect.Emitters[0] = emitter;
			effect.Emitters[0].MaxParticles = 5;
			effect.Emitters[0].Rate = ParticleValue::constant(1000.0f);
			check(spawned(plain, 0.5f, 60.0f, lookup) <= 5 * 30, "particle_ticks: no frame spawns more than the emitter's cap");
			effect.Emitters[0] = emitter;

			// steps that are not times
			{
				Scene scene;
				const entt::entity entity = scene.create("fx");
				ParticleSystem system;
				system.Effect = reference;
				scene.registry().emplace<ParticleSystem>(entity, system);
				tick_particle_systems(scene, 0.0f, lookup);
				tick_particle_systems(scene, -1.0f, lookup);
				tick_particle_systems(scene, std::numeric_limits<float>::quiet_NaN(), lookup);
				const ParticleRuntime& runtime = scene.registry().get<ParticleRuntime>(entity);
				check(runtime.Time == 0.0f && runtime.SpawnCount[0] == 0, "particle_ticks: a step of 0, a negative one or NaN advances nothing");
				tick_particle_systems(scene, 100.0f, lookup);
				check(std::abs(scene.registry().get<ParticleRuntime>(entity).Time - 0.1f) < 1e-5f && scene.registry().get<ParticleRuntime>(entity).SpawnCount[0] <= 2, "particle_ticks: a stall is cut to a 0.1 s step (no flood)");
			}

			// a missing effect waits; an unset one too
			check(spawned(plain, 2.0f, 60.0f, [](const AssetRef&) -> const ParticleEffectData* { return nullptr; }) == 0, "particle_ticks: an effect that is not loaded spawns nothing and does not crash");

			// a system that is done and asked to remove itself goes away
			{
				Scene scene;
				const entt::entity entity = scene.create("fx");
				ParticleSystem system;
				system.Effect = reference;
				scene.registry().emplace<ParticleSystem>(entity, system);
				tick_particle_systems(scene, 0.016f, lookup);
				scene.registry().get<ParticleRuntime>(entity).SelfDestruct = true;
				for (int frame = 0; frame < 60 * 6; frame++)
				{
					tick_particle_systems(scene, 1.0f / 60.0f, lookup);
				}
				check(scene.registry().valid(entity), "particle_ticks: a system that still emits stays");
				for (int frame = 0; frame < 60 * 3; frame++)
				{
					tick_particle_systems(scene, 1.0f / 60.0f, lookup);
				}
				check(!scene.registry().valid(entity), "particle_ticks: a finished system with SelfDestruct is destroyed after its last particle");
			}
		}

		// CharacterController survives save, cook, load (binary and the text of the file) and a prefab override of StepHeight applies.
		void check_character_data(Check& check)
		{
			register_scene_components();
			check(ComponentRegistry::get().find("CharacterController") != nullptr, "character: CharacterController is registered");

			Scene source;
			const EntityGuid ownerId = EntityGuid::generate();
			const entt::entity owner = source.create(ownerId, "Hero");
			CharacterController controller;
			controller.Radius = 0.33f;
			controller.Height = 1.7f;
			controller.Offset = glm::vec3(0.0f, 0.1f, 0.0f);
			controller.StepHeight = 0.4f;
			controller.MaxSlopeAngle = 50.0f;
			controller.Mass = 80.0f;
			controller.PushStrength = 150.0f;
			controller.GravityFactor = 1.5f;
			controller.Layer = 3;
			source.registry().emplace<CharacterController>(owner, controller);

			const SceneData first = data_from_scene(source, "character");
			const std::vector<std::byte> bytes = serialize(first);
			std::optional<SceneData> back = deserialize<SceneData>(bytes);
			check(back.has_value(), "character: the scene survives the cooked format");
			if (!back)
			{
				return;
			}
			Scene copy;
			const ApplyReport report = apply_scene_data(copy, *back);
			check(report.UnknownComponents == 0 && report.UnknownFields == 0, "character: nothing in the saved scene is unknown");
			const entt::entity loaded = copy.find(ownerId);
			check(loaded != entt::null && copy.registry().all_of<CharacterController>(loaded), "character: the entity keeps its CharacterController");
			if (loaded != entt::null && copy.registry().all_of<CharacterController>(loaded))
			{
				const CharacterController& c = copy.registry().get<CharacterController>(loaded);
				check(c.Radius == 0.33f && c.Height == 1.7f && c.Offset == controller.Offset, "character: the capsule survives");
				check(c.StepHeight == 0.4f && c.MaxSlopeAngle == 50.0f, "character: step height and slope survive");
				check(c.Mass == 80.0f && c.PushStrength == 150.0f && c.GravityFactor == 1.5f && c.Layer == 3, "character: mass, push, gravity and layer survive");
			}
			check(serialize(data_from_scene(copy, "character")) == bytes, "character: save, load, save gives the same bytes");

			const SceneJsonResult viaJson = scene_from_json(SceneJson::parse(scene_to_json(first).dump(2)));
			check(viaJson.Scene.has_value() && serialize(*viaJson.Scene) == bytes, "character: the text of the file gives the same binary scene (" + viaJson.Error + ")");

			PrefabData prefab;
			prefab.Name = "hero";
			PrefabNode node = make_node("Hero", -1);
			node.Components.push_back(ComponentRecord{ "CharacterController", { FieldRecord{ "StepHeight", FieldValue(0.35f) } } });
			prefab.Nodes = { node };
			assign_stable_ids(prefab.Nodes);
			const u64 nodeId = prefab.Nodes[0].StableId;
			EntityRecord instance = make_instance(EntityGuid::generate(), AssetRef{ AssetGuid::generate(), 0 });
			instance.Overrides = { set_field_override(nodeId, "CharacterController", "StepHeight", FieldValue(0.5f)) };
			const ExpandResult expanded = expand_prefab(instance, prefab);
			const FieldValue* step = expanded.Entities.size() == 2 ? find_field(expanded.Entities[1].Components, "CharacterController", "StepHeight") : nullptr;
			check(step != nullptr && std::get_if<float>(step) != nullptr && *std::get_if<float>(step) == 0.5f, "character: a prefab override of CharacterController.StepHeight applies");
		}
	}

	bool run_scene_smoke_test(AssetPipelineModule& pipeline)
	{
		Check check;

		check_scene_basics(check);
		check_reflection(check);
		check_render_components(check);
		check_scene_data(check);
		check_physics_data(check);
		check_joint_data(check);
		check_decal_data(check);
		check_decal_lifetime(check);
		check_particle_system_data(check);
		check_particle_ticks(check);
		check_character_data(check);
		check_node_ids(check, pipeline);
		check_node_components(check);
		check_prefab(check);
		check_scene_json(check, pipeline);
		check_scene_loading(check, pipeline);
		check_extracted_materials(check, pipeline);
		check_make_prefab(check, pipeline);
		check_duplicate_rename(check, pipeline);
		check_scene_editor(check, pipeline);

		if (check.Failures == 0)
		{
			WI_SCENE_LOG(Info, "Scene smoke test passed");
		}
		return check.Failures == 0;
	}
}
