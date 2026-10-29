#pragma once

#include <string>

#include <entt/entt.hpp>
#include <math/math.h>

#include <assets/asset_guid.h>
#include "entity_guid.h"
#include "scene_data.h"

// The components of a scene. They are plain data; the reflection layer (reflection/) describes their fields for the
// serializer and the editor. Assets are referenced by AssetRef, never by a loaded handle.
namespace wiwa
{
	// Stable identity of the entity inside its scene file.
	struct Id
	{
		EntityGuid Guid{};
	};

	struct Name
	{
		std::string Value{};
	};

	// Local to the parent.
	struct Transform
	{
		glm::vec3 Position{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 Scale{ 1.0f };
	};

	// Parent and children as a linked list. Kept by Scene; change it through Scene::set_parent only.
	struct Hierarchy
	{
		entt::entity Parent{ entt::null };
		entt::entity FirstChild{ entt::null };
		entt::entity NextSibling{ entt::null };
	};

	// Computed from the Transforms by Scene::update_world_transforms; not saved.
	struct WorldTransform
	{
		glm::mat4 Matrix{ 1.0f };
	};

	enum class ProjectionKind : u32
	{
		Perspective,
		Orthographic,
	};

	struct Camera
	{
		ProjectionKind Projection = ProjectionKind::Perspective;
		float FovY = 60.0f; // degrees
		float Near = 0.1f;
		float Far = 1000.0f;
		float OrthoSize = 10.0f; // half the height of the view
		bool Primary = false;
	};

	enum class LightKind : u32
	{
		Directional,
		Point,
		Spot,
	};

	struct Light
	{
		LightKind Kind = LightKind::Point;
		glm::vec4 Color{ 1.0f };
		float Intensity = 1.0f;
		float Range = 10.0f;
		float InnerAngle = 20.0f; // degrees, spot only
		float OuterAngle = 30.0f;
		bool CastsShadows = false;
	};

	struct MeshRenderer
	{
		AssetRef Mesh{};
		std::vector<AssetRef> Materials{}; // overrides, per primitive; empty means the mesh's own materials
	};

	// The root of a linked prefab instance: the entities below it are generated from the Prefab, and the overrides change them.
	struct PrefabInstance
	{
		AssetRef Prefab{};
		std::vector<PrefabOverride> Overrides{};
	};
}
