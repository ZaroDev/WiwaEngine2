#include <editor_common.h>
#include "scene_smoke_test.h"

#include <scene/components.h>
#include <scene/entity_guid.h>
#include <scene/reflection/component_registry.h>
#include <scene/reflection/value_bag.h>
#include <scene/scene.h>
#include <scene/scene_components.h>

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
	}

	bool run_scene_smoke_test(AssetPipelineModule& pipeline)
	{
		Check check;

		check_scene_basics(check);
		check_reflection(check);
		check_render_components(check);

		if (check.Failures == 0)
		{
			WI_SCENE_LOG(Info, "Scene smoke test passed");
		}
		return check.Failures == 0;
	}
}
