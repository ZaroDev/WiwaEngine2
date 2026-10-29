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
	}

	bool run_scene_smoke_test(AssetPipelineModule& pipeline)
	{
		Check check;

		check_scene_basics(check);
		check_reflection(check);

		if (check.Failures == 0)
		{
			WI_SCENE_LOG(Info, "Scene smoke test passed");
		}
		return check.Failures == 0;
	}
}
