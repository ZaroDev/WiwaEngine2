#include <editor_common.h>
#include "scene_smoke_test.h"

#include <scene/components.h>
#include <assets/wasset_io.h>
#include <assets/asset_manager.h>
#include <core/engine.h>
#include <scene/entity_guid.h>
#include <scene/prefab_node_id.h>
#include <scene/reflection/component_registry.h>
#include <scene/reflection/value_bag.h>
#include <scene/scene.h>
#include <scene/scene_components.h>
#include <scene/scene_records.h>

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
			check(mesh != nullptr && field_names(*mesh) == std::vector<std::string>{ "Mesh", "Materials" }, "MeshRenderer lists its fields");
			if (mesh != nullptr && mesh->Fields.size() == 2)
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

			const std::vector<AssetRef> references = collect_references(first);
			check(references.size() == 5, "collect_references finds each referenced asset once (found " + std::to_string(references.size()) + ")");
			auto has_reference = [&](const AssetRef& ref) { return std::find(references.begin(), references.end(), ref) != references.end(); };
			check(has_reference(mesh) && has_reference(materialA) && has_reference(materialB) && has_reference(prefab) && has_reference(texture), "collect_references names the mesh, materials, prefab and the ref inside an override");

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
	}

	bool run_scene_smoke_test(AssetPipelineModule& pipeline)
	{
		Check check;

		check_scene_basics(check);
		check_reflection(check);
		check_render_components(check);
		check_scene_data(check);
		check_node_ids(check, pipeline);

		if (check.Failures == 0)
		{
			WI_SCENE_LOG(Info, "Scene smoke test passed");
		}
		return check.Failures == 0;
	}
}
