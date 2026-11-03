#pragma once

#include <string>

#include <entt/entt.hpp>
#include <math/math.h>

#include <assets/asset_guid.h>
#include "entity_guid.h"
#include "scene_data.h"
#include <physics/physics_types.h>

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

	// On an entity that a prefab instance generated (or that one of its AddChild overrides made): which instance, and which node
	// of its prefab (ADDED_CHILD_NODE for an added child). Not saved: the instance's record is what the scene file holds.
	struct PrefabChild
	{
		EntityGuid Instance{};
		u64 NodeId = 0;
	};

	// Components and fields that were in the scene file but that this engine does not know. They are kept so that saving the
	// scene does not lose data written by a newer engine. A record that names a known component holds only its unknown fields.
	struct UnknownComponents
	{
		std::vector<ComponentRecord> Records{};
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

	// Shows a UI panel over the scene. Canvases are drawn in the order of SortOrder (the higher on top), and the one on top gets the pointer
	// first. Every canvas has its own copy of the panel's state, so two canvases of one panel do not share what scripts set.
	struct UICanvas
	{
		AssetRef Panel{};
		i32 SortOrder = 0;
		bool Visible = true;
	};

	// A physics body. The shape comes from the Collider on the same entity (and on its descendants, which join it as a compound).
	// Lock flags are the axes the body may NOT move or turn along.
	struct RigidBody
	{
		MotionType Motion = MotionType::Dynamic;
		float MassOverride = 0.0f; // 0: from the shape and its material density
		float LinearDamping = 0.05f;
		float AngularDamping = 0.05f;
		float GravityFactor = 1.0f;
		bool ContinuousCollision = false;
		bool AllowSleeping = true;
		bool ReportContacts = false;
		bool LockPositionX = false;
		bool LockPositionY = false;
		bool LockPositionZ = false;
		bool LockRotationX = false;
		bool LockRotationY = false;
		bool LockRotationZ = false;
	};

	// Only append: the values are saved. A compound shape is implied by child colliders and has no value here.
	enum class ColliderShape : u32
	{
		Box,
		Sphere,
		Capsule,
		Cylinder,
		ConvexHull,
		Mesh,
	};

	struct Collider
	{
		ColliderShape Shape = ColliderShape::Box;
		glm::vec3 Size{ 0.5f };          // half extents (box), radius (sphere: x), radius and half height (capsule, cylinder: x and y)
		glm::vec3 Offset{ 0.0f };
		glm::vec3 RotationEuler{ 0.0f }; // degrees
		bool IsTrigger = false;
		i32 Layer = 0;                   // an index into the project's physics layers
		AssetRef Material{};             // PhysicsMaterial
		AssetRef Mesh{};                 // Mesh, for ConvexHull and Mesh
	};
}
