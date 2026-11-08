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

	// A constraint between this entity's body (A) and the body of Connected (B; not set: the world). Anchors are in each body's local
	// space (the world point when Connected is not set); angles are degrees. Only the fields the Type uses count. LimitMin == LimitMax == 0
	// means no limit; a break value of 0 means unbreakable. Hinge: limits are angles; Slider: distances along Axis; Distance: min and max
	// length (both 0: the length at the start). Cone and SwingTwist use the half angles.
	struct Joint
	{
		JointType Type = JointType::Hinge;
		EntityGuid Connected{};
		bool CollideConnected = false;
		glm::vec3 AnchorA{ 0.0f };
		glm::vec3 AnchorB{ 0.0f };
		glm::vec3 Axis{ 0.0f, 1.0f, 0.0f };
		glm::vec3 NormalAxis{ 1.0f, 0.0f, 0.0f };
		float LimitMin = 0.0f;
		float LimitMax = 0.0f;
		float SwingHalfAngle = 45.0f;
		float TwistHalfAngle = 45.0f;
		float SpringFrequency = 0.0f; // Hz; 0: rigid
		float SpringDamping = 0.0f;
		JointMotorMode MotorMode = JointMotorMode::Off;
		float MotorTarget = 0.0f;     // velocity (rad/s or m/s) or position (degrees or m)
		float MotorMaxForce = 0.0f;   // N or N m
		float BreakForce = 0.0f;
		float BreakTorque = 0.0f;
	};

	// A character the scripts move (physics.md, Character controller): a capsule that slides along what it meets, climbs steps and stands on
	// slopes up to MaxSlopeAngle, without being a rigid body. Up is world +Y. Put it on an entity with no Collider or RigidBody of its own.
	// Height is the whole capsule, caps included (a Height below 2 * Radius is made 2 * Radius); Offset moves the capsule from the entity's
	// origin; angles are degrees.
	struct CharacterController
	{
		float Radius = 0.4f;
		float Height = 1.8f;
		glm::vec3 Offset{ 0.0f, 0.0f, 0.0f };
		float StepHeight = 0.35f;
		float MaxSlopeAngle = 45.0f;
		float Mass = 70.0f;           // kg, for what it pushes and what pushes it
		float PushStrength = 100.0f;  // the most force (N) it pushes a body with
		float GravityFactor = 1.0f;
		i32 Layer = 0;                // an index into the project's physics layers
	};
	// Plays the clips of a model on its skeleton (animation.md). Put on the root of the model: the prefab expander adds it. The runtime
	// state (the pose, the nodes being blended) is not a component: the AnimationModule keeps it per Animator.
	struct Animator
	{
		AssetRef Skeleton{};
		std::vector<AssetRef> Clips{}; // the model's animations; a clip is played by its name
		AssetRef Clip{};               // played when the scene starts, if PlayOnStart
		bool PlayOnStart = true;
		float Speed = 1.0f;
		bool Enabled = true;
		float BoundsPadding = 0.0f;    // the mesh bounds grow by this much in every direction for culling
		AssetRef Graph{};              // an animation graph (.wgraph) that plays from the scene start instead of Clip
	};

	// On any descendant of an Animator: follows a joint of its skeleton. After the pose is evaluated the entity's Transform is written.
	struct AnimationSocket
	{
		std::string Joint{};
		glm::vec3 OffsetPosition{ 0.0f };
		glm::quat OffsetRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
	};

	// What a ViewHost's preview is drawn with (the profiles of renderer/view/view_types.h; the values match).
	enum class ViewHostProfile : u32 { Full, Preview, Thumbnail };

	// Shows a prefab in a small scene of its own, drawn into a texture: a 3D widget for the game UI (a ViewImage widget names the host). Everything is
	// looked at from an orbit camera; SpinSpeed turns the model. The view exists while the entity does (views.md).
	struct ViewHost
	{
		std::string Name{};          // what a ViewImage widget calls it; unique in the scene
		AssetRef Model{};            // the prefab to show
		glm::vec3 Target{ 0.0f, 1.0f, 0.0f }; // the point the camera looks at
		float Distance = 4.0f;
		float Yaw = 20.0f;           // degrees about the vertical axis
		float Pitch = 10.0f;         // degrees above the target
		float Fov = 35.0f;
		float SpinSpeed = 0.0f;      // degrees per second the model turns about the vertical axis
		i32 Width = 256;             // pixels of the texture
		i32 Height = 256;
		ViewHostProfile Profile = ViewHostProfile::Preview;
		bool Transparent = true;     // nothing behind the model: the widget shows what is under it
		bool Continuous = true;      // false: drawn again only when something above changed

		bool operator==(const ViewHost&) const = default;
	};

	// On a skinned mesh node, made by the prefab expander: the skeleton that deforms the mesh. The Animator that drives it is the nearest
	// ancestor with the same skeleton.
	struct SkinBinding
	{
		AssetRef Skeleton{};
	};
}
