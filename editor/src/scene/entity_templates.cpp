#include <editor_common.h>
#include "entity_templates.h"

namespace
{
	constexpr const char* SHAPES_MODEL = "Assets/models/shapes/shapes.glb";

	struct Shape
	{
		const char* Name;
		wiwa::u64 LocalId;
	};

	constexpr Shape CUBE{ "Cube", 1 };
	constexpr Shape SPHERE{ "Sphere", 2 };
	constexpr Shape CYLINDER{ "Cylinder", 3 };
	constexpr Shape CONE{ "Cone", 4 };
	constexpr Shape TORUS{ "Torus", 5 };
	constexpr Shape PLANE{ "Plane", 6 };

	using Setup = std::function<void(wiwa::SceneEditor&, entt::entity)>;

	// An entity with the named components added (Name and Transform exist already), then the setup.
	entt::entity make(wiwa::SceneEditor& editor, entt::entity parent, const std::string& name, const std::vector<std::string>& components, const Setup& setup = {})
	{
		const entt::entity entity = editor.create_entity(parent);
		if (entity == entt::null)
		{
			return entt::null;
		}
		editor.rename_entity(entity, name);
		for (const std::string& component : components)
		{
			editor.add_component(entity, component);
		}
		if (setup)
		{
			setup(editor, entity);
		}
		return entity;
	}

	wiwa::AssetRef shape_ref(const wiwa::AssetRef& model, const Shape& shape)
	{
		return wiwa::AssetRef{ model.Guid, shape.LocalId };
	}

	wiwa::FieldValue light_kind(wiwa::LightKind kind)
	{
		return wiwa::FieldValue(static_cast<wiwa::i32>(kind));
	}

	void rotate(wiwa::SceneEditor& editor, entt::entity entity, glm::vec3 degrees)
	{
		editor.set_field(entity, "Transform", "Rotation", wiwa::FieldValue(glm::quat(glm::radians(degrees))));
	}
}

std::vector<EntityTemplate> entity_templates(wiwa::AssetManager& assets)
{
	using namespace wiwa;
	std::vector<EntityTemplate> templates;

	templates.push_back({ "Empty", "", [](SceneEditor& editor, entt::entity parent) { return make(editor, parent, "Entity", {}); } });

	if (const std::optional<AssetRef> model = assets.find_asset(SHAPES_MODEL))
	{
		const AssetRef shapes = *model;
		for (const Shape& shape : { CUBE, SPHERE, CYLINDER, CONE, TORUS, PLANE })
		{
			templates.push_back({ shape.Name, "3D", [shapes, shape](SceneEditor& editor, entt::entity parent)
			{
				return make(editor, parent, shape.Name, { "MeshRenderer" }, [&](SceneEditor& e, entt::entity entity)
				{
					e.set_field(entity, "MeshRenderer", "Mesh", FieldValue(shape_ref(shapes, shape)));
				});
			} });
		}

		for (const Shape& shape : { CUBE, SPHERE })
		{
			const std::string name = std::string("Rigidbody ") + shape.Name;
			templates.push_back({ name, "Physics", [shapes, shape, name](SceneEditor& editor, entt::entity parent)
			{
				return make(editor, parent, name, { "MeshRenderer", "Collider", "RigidBody" }, [&](SceneEditor& e, entt::entity entity)
				{
					e.set_field(entity, "MeshRenderer", "Mesh", FieldValue(shape_ref(shapes, shape)));
					e.set_field(entity, "Collider", "Shape", FieldValue(static_cast<i32>(shape.LocalId == SPHERE.LocalId ? ColliderShape::Sphere : ColliderShape::Box)));
				});
			} });
		}
	}

	templates.push_back({ "Directional light", "Light", [](SceneEditor& editor, entt::entity parent)
	{
		return make(editor, parent, "Directional light", { "Light" }, [](SceneEditor& e, entt::entity entity)
		{
			e.set_field(entity, "Light", "Kind", light_kind(LightKind::Directional));
			rotate(e, entity, glm::vec3(-50.0f, 30.0f, 0.0f));
		});
	} });
	templates.push_back({ "Point light", "Light", [](SceneEditor& editor, entt::entity parent)
	{
		return make(editor, parent, "Point light", { "Light" }, [](SceneEditor& e, entt::entity entity)
		{
			e.set_field(entity, "Light", "Kind", light_kind(LightKind::Point));
		});
	} });
	templates.push_back({ "Spot light", "Light", [](SceneEditor& editor, entt::entity parent)
	{
		return make(editor, parent, "Spot light", { "Light" }, [](SceneEditor& e, entt::entity entity)
		{
			e.set_field(entity, "Light", "Kind", light_kind(LightKind::Spot));
			rotate(e, entity, glm::vec3(-90.0f, 0.0f, 0.0f));
		});
	} });

	templates.push_back({ "Camera", "", [](SceneEditor& editor, entt::entity parent) { return make(editor, parent, "Camera", { "Camera" }); } });
	templates.push_back({ "UI Canvas", "UI", [](SceneEditor& editor, entt::entity parent) { return make(editor, parent, "UI Canvas", { "UICanvas" }); } });
	templates.push_back({ "Animator", "Animation", [](SceneEditor& editor, entt::entity parent) { return make(editor, parent, "Animated object", { "Animator" }); } });
	templates.push_back({ "Character controller", "Physics", [](SceneEditor& editor, entt::entity parent) { return make(editor, parent, "Character", { "CharacterController" }); } });

	return templates;
}
