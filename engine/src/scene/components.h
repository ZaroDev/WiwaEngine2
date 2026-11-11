#pragma once

#include <string>

#include <entt/entt.hpp>
#include <math/math.h>

#include <assets/asset_guid.h>
#include "entity_guid.h"
#include "scene_data.h"
#include <physics/physics_types.h>
#include <nav/nav_types.h>

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
		bool NestedRoot = false; // the node is the root of a nested prefab instance (its children come from that prefab)
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
		bool ReceivesDecals = true;        // false: decals never draw on this mesh
	};

	// Projects a Material (base color, normal, ORM, emissive, alpha) onto the opaque geometry inside its box. The box is Size in local
	// space, projecting along local -Z; the transform's rotation and scale apply. Fades run only while the game clock plays.
	struct Decal
	{
		AssetRef Material{};
		glm::vec3 Size{ 1.0f };
		float Opacity = 1.0f;
		bool AffectColor = true;
		bool AffectNormal = true;
		bool AffectOrm = true;
		bool AffectEmissive = true;
		float NormalAngleFadeStart = 60.0f; // degrees between the surface normal and the projection axis
		float NormalAngleFadeEnd = 80.0f;
		float EdgeFade = 0.1f; // fraction of the box faded at its sides
		i32 SortOrder = 0;     // higher draws later and wins overlaps
		float FadeInTime = 0.0f;
		float Lifetime = 0.0f; // seconds; 0 is permanent
		float FadeOutTime = 0.0f;
	};

	// Plays a particle effect (.wfx, vfx.md). The particles are simulated on the GPU; this component says which effect, whether it plays, and a few
	// parameters scripts and prefabs can change. It plays only while the game clock plays (the editor previews the selected one).
	struct ParticleSystem
	{
		AssetRef Effect{};
		bool PlayOnStart = true;
		bool Looping = true;        // false: every emitter plays once, whatever its own Looping says
		float TimeScale = 1.0f;     // 0 pauses, 2 is twice as fast
		i32 SortBias = 0;           // orders the effect among the others that blend: the higher draws later
		glm::vec4 ColorTint{ 1.0f };// multiplies every particle's color
		float SizeScale = 1.0f;     // multiplies every particle's size
		float RateScale = 1.0f;     // multiplies the emission rate (bursts are not scaled)
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
		glm::vec3 OffsetScale{ 1.0f };
	};

	// Hears the scene: the first active listener (else the primary camera) is where sound is heard from.
	struct AudioListener
	{
		bool Active = true;
	};

	// Plays audio events from its entity's position while the game plays.
	struct AudioEmitter
	{
		std::string Event{};              // the vendor's event name (Wwise: Play_Footstep); empty plays nothing
		bool PlayOnStart = false;         // posts Event once when the emitter starts to exist in a playing scene
		float AttenuationScale = 1.0f;    // scales the event's attenuation distances
	};

	// The region of the scene a navmesh is baked for (navigation.md). NavMesh is the baked .wnav; the rest are what Recast bakes with, the fields of
	// NavBuildSettings one by one (build() puts them together). The box is Size around the entity's position. Masks are bit sets held in an i32
	// (-1: all): LayerMask picks the render layers that contribute geometry.
	struct NavMeshVolume
	{
		AssetRef NavMesh{};
		glm::vec3 Size{ 20.0f, 6.0f, 20.0f };
		float AgentRadius = 0.4f;
		float AgentHeight = 1.8f;
		float AgentMaxClimb = 0.4f;
		float AgentMaxSlope = 45.0f;
		float CellSize = 0.2f;
		float CellHeight = 0.1f;
		i32 RegionMinSize = 8;
		i32 RegionMergeSize = 20;
		float EdgeMaxLength = 12.0f;
		float EdgeMaxError = 1.3f;
		i32 VertsPerPoly = 6;
		float DetailSampleDist = 6.0f;
		float DetailSampleMaxError = 1.0f;
		i32 TileSize = 64;
		i32 LayerMask = -1;
		bool Enabled = true;

		NavBuildSettings build() const
		{
			NavBuildSettings settings;
			settings.AgentRadius = AgentRadius;
			settings.AgentHeight = AgentHeight;
			settings.AgentMaxClimb = AgentMaxClimb;
			settings.AgentMaxSlope = AgentMaxSlope;
			settings.CellSize = CellSize;
			settings.CellHeight = CellHeight;
			settings.RegionMinSize = RegionMinSize;
			settings.RegionMergeSize = RegionMergeSize;
			settings.EdgeMaxLength = EdgeMaxLength;
			settings.EdgeMaxError = EdgeMaxError;
			settings.VertsPerPoly = VertsPerPoly;
			settings.DetailSampleDist = DetailSampleDist;
			settings.DetailSampleMaxError = DetailSampleMaxError;
			settings.TileSize = TileSize;
			return settings;
		}

		void set_build(const NavBuildSettings& settings)
		{
			AgentRadius = settings.AgentRadius;
			AgentHeight = settings.AgentHeight;
			AgentMaxClimb = settings.AgentMaxClimb;
			AgentMaxSlope = settings.AgentMaxSlope;
			CellSize = settings.CellSize;
			CellHeight = settings.CellHeight;
			RegionMinSize = settings.RegionMinSize;
			RegionMergeSize = settings.RegionMergeSize;
			EdgeMaxLength = settings.EdgeMaxLength;
			EdgeMaxError = settings.EdgeMaxError;
			VertsPerPoly = settings.VertsPerPoly;
			DetailSampleDist = settings.DetailSampleDist;
			DetailSampleMaxError = settings.DetailSampleMaxError;
			TileSize = settings.TileSize;
		}
	};

	// Marks the geometry of the entity's mesh as one kind of ground when a navmesh is baked. NotWalkable makes it an obstacle.
	struct NavAreaTag
	{
		NavArea Area = NavArea::Walkable;
	};

	// Walks the navmesh to a destination while the game plays (NavModule moves the transform). The state of the walk lives in the module.
	// AreaMask is a bit set of the areas it may walk on (-1: all); with DriveTransform off the module only reports the velocity (for a character body).
	struct NavAgent
	{
		float Speed = 3.5f;
		float Acceleration = 8.0f;
		float Radius = 0.4f;
		float Height = 1.8f;
		float StoppingDistance = 0.2f;
		i32 AvoidanceQuality = 2;     // 0 none .. 3 best
		float SeparationWeight = 2.0f;
		i32 AreaMask = -1;
		bool AutoRepath = true;
		bool DriveTransform = true;
	};

	// Runs a behaviour tree (.wbt) for the entity while the game plays (ai/ai_module.h). TickRate is in Hz (0: every frame). Overrides is a JSON object of
	// blackboard key to value that replaces the tree's defaults for this entity. The state of the run lives in the AIModule, not here.
	struct AIController
	{
		AssetRef Tree{};
		bool Enabled = true;
		float TickRate = 0.0f;
		i32 Seed = 0;
		std::string Overrides{};
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
