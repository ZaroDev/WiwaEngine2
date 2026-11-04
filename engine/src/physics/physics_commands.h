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

	// Body control from scripts. All take effect in the next step. AddForce and AddTorque act during that one step only; AddImpulse changes
	// the velocity once; a command for a body that is not dynamic (or is gone) is ignored.
	struct AddForce
	{
		BodyKey Key{};
		glm::vec3 Force{ 0.0f }; // newtons, world space, through the centre of mass
	};

	struct AddTorque
	{
		BodyKey Key{};
		glm::vec3 Torque{ 0.0f }; // newton metres, world space
	};

	struct AddImpulse
	{
		BodyKey Key{};
		glm::vec3 Impulse{ 0.0f }; // newton seconds, world space, through the centre of mass
	};

	struct SetVelocity
	{
		BodyKey Key{};
		glm::vec3 Linear{ 0.0f }; // m/s
	};

	struct SetAngularVelocity
	{
		BodyKey Key{};
		glm::vec3 Angular{ 0.0f }; // rad/s
	};

	// Wakes a sleeping body.
	struct WakeBody
	{
		BodyKey Key{};
	};

	// Gravity of the world (the scene's setting is applied again only by a SetSettings).
	struct SetGravity
	{
		glm::vec3 Gravity{ 0.0f, -9.81f, 0.0f };
	};

	// Turns the recording of the colliders' debug drawing on or off (into each step's snapshot). Ignored where Jolt has no debug renderer (Shipping).
	struct SetDebugDraw
	{
		bool Enabled = false;
	};

	// A constraint between two bodies, plain data (see the Joint component for what the fields mean). A is the owner's body; B is the
	// other body, a null guid for the world (the anchors are then world points). Angles are degrees.
	struct JointDesc
	{
		JointType Type = JointType::Hinge;
		BodyKey A{};
		BodyKey B{};
		bool CollideConnected = false;
		glm::vec3 AnchorA{ 0.0f };
		glm::vec3 AnchorB{ 0.0f };
		glm::vec3 Axis{ 0.0f, 1.0f, 0.0f };
		glm::vec3 NormalAxis{ 1.0f, 0.0f, 0.0f };
		float LimitMin = 0.0f;
		float LimitMax = 0.0f;
		float SwingHalfAngle = 45.0f;
		float TwistHalfAngle = 45.0f;
		float SpringFrequency = 0.0f;
		float SpringDamping = 0.0f;
		JointMotorMode MotorMode = JointMotorMode::Off;
		float MotorTarget = 0.0f;
		float MotorMaxForce = 0.0f;
		float BreakForce = 0.0f;
		float BreakTorque = 0.0f;
	};

	// Makes the joint owned by Desc.A (one that exists for that owner is replaced). It waits, unmade, until both bodies exist.
	struct CreateJoint
	{
		JointDesc Desc{};
	};

	struct DestroyJoint
	{
		BodyKey Owner{};
	};

	// Changes the motor of a joint (Off turns it off). Effective from the next step.
	struct SetJointMotor
	{
		BodyKey Owner{};
		JointMotorMode Mode = JointMotorMode::Off;
		float Target = 0.0f;
		float MaxForce = 0.0f;
	};

	// Changes the limits of a joint (degrees for a hinge, metres for a slider and a distance joint).
	struct SetJointLimits
	{
		BodyKey Owner{};
		float Min = 0.0f;
		float Max = 0.0f;
	};

	// Plain-data commands from the main thread to the physics thread, applied at the start of the next step in submission order.
	// New kinds are added as new alternatives. Commands for a key that has no body are ignored.
	using PhysicsCommand = std::variant<SetSettings, CreateBody, DestroyBody, SetTransform, SetKinematicTarget, ReplaceShape, SetMaterial, SetLayer, SetLayers, AddForce, AddTorque, AddImpulse, SetVelocity, SetAngularVelocity, WakeBody, SetGravity, SetDebugDraw, CreateJoint, DestroyJoint, SetJointMotor, SetJointLimits>;
}
