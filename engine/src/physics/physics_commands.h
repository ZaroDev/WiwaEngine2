#pragma once

#include <memory>
#include <variant>
#include <vector>

#include "cooked_shape.h"
#include "physics_layers.h"
#include "physics_material_asset.h"
#include "physics_types.h"

namespace wiwa
{
	// Replaces the step settings (rate, gravity, iterations, sleep threshold) from the next step on.
	struct SetSettings
	{
		PhysicsSettings Settings{};
	};

	// What a shape is made of. Placeholder: a shape the physics world cannot build (a ConvexHull or Mesh collider whose cooked shape is
	// missing or failed): the body is made but collides with nothing. Compound: the Children are the parts.
	enum class ShapeKind : u32 { Box, Sphere, Capsule, Cylinder, Placeholder, Compound, ConvexHull, Mesh };

	// One shape of a body, plain data. Size is as the Collider says (box: half extents; sphere: radius in x; capsule and cylinder: radius
	// in x, half height of the cylinder part in y), before the scale; the physics thread applies Scale (the entity's world scale) and
	// clamps. Offset and Rotation place the shape in the body's frame (its entity's world position and rotation, no scale), Offset
	// already scaled. A compound has Kind Compound and its parts in Children (each placed the same way); a body with one collider has that
	// collider's shape directly.
	struct ShapeDesc
	{
		ShapeKind Kind = ShapeKind::Box;
		glm::vec3 Size{ 0.5f };
		glm::vec3 Offset{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 Scale{ 1.0f };
		PhysicsMaterialData Material{}; // the Collider's PhysicsMaterial asset, or the default; plain data, resolved by the main thread
		EntityGuid Source{}; // the entity whose Collider this is, for warnings that are logged once per collider
		std::shared_ptr<const CookedShapeData> Cooked{}; // ConvexHull and Mesh: the cooked shape (null while it loads or when it failed: the part does not collide)
		std::vector<ShapeDesc> Children{};
	};

	// Everything about a body except its shape.
	struct BodyDesc
	{
		MotionType Motion = MotionType::Static;
		float MassOverride = 0.0f; // 0 (or less): from the shape and its density
		float LinearDamping = 0.05f;
		float AngularDamping = 0.05f;
		float GravityFactor = 1.0f;
		bool ContinuousCollision = false;
		bool AllowSleeping = true;
		bool ReportContacts = false;
		bool LockPosition[3] = { false, false, false }; // true: the body may not move along that world axis
		bool LockRotation[3] = { false, false, false }; // true: the body may not turn around that world axis
		i32 Layer = 0;  // project layer index, clamped to 0..31 by the physics thread
		bool IsTrigger = false;
		glm::vec3 Position{ 0.0f }; // initial pose
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		ShapeDesc Shape{};
	};

	// Makes the body of that key (one that exists is replaced).
	struct CreateBody
	{
		BodyKey Key{};
		BodyDesc Desc{};
	};

	struct DestroyBody
	{
		BodyKey Key{};
	};

	// Teleports a body (static ones are moved this way).
	struct SetTransform
	{
		BodyKey Key{};
		glm::vec3 Pos{ 0.0f };
		glm::quat Rot{ 1.0f, 0.0f, 0.0f, 0.0f };
	};

	// Moves a kinematic body to this pose over the next step (it pushes what it meets) and stops it there.
	struct SetKinematicTarget
	{
		BodyKey Key{};
		glm::vec3 Pos{ 0.0f };
		glm::quat Rot{ 1.0f, 0.0f, 0.0f, 0.0f };
	};

	// Gives a body a new shape (a collider changed, or a compound part was added or removed). The mass is recomputed.
	struct ReplaceShape
	{
		BodyKey Key{};
		ShapeDesc Shape{};
	};

	// Gives every part of a body's shape this material (a Collider's material asset finished loading, or was changed). The shape is
	// rebuilt, so the mass follows the new density.
	struct SetMaterial
	{
		BodyKey Key{};
		PhysicsMaterialData Material{};
	};

	// Puts a body on another project layer (0..31, clamped), keeping the body and its contacts' bookkeeping. A sleeping body is woken.
	struct SetLayer
	{
		BodyKey Key{};
		i32 Layer = 0;
	};

	// Replaces the project layers' collision matrix (the world starts with PhysicsLayers' default: everything collides). Pairs already
	// touching are tested again on the next step.
	struct SetLayers
	{
		PhysicsLayers Layers{};
	};

	// Plain-data commands from the main thread to the physics thread, applied at the start of the next step in submission order.
	// New kinds are added as new alternatives. Commands for a key that has no body are ignored.
	using PhysicsCommand = std::variant<SetSettings, CreateBody, DestroyBody, SetTransform, SetKinematicTarget, ReplaceShape, SetMaterial, SetLayer, SetLayers>;
}
