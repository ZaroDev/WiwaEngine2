#pragma once

#include <memory>
#include <set>
#include <unordered_map>

#include <assets/handle.h>

#include <scene/scene.h>

#include "physics_material_asset.h"
#include "physics_snapshot.h"
#include "physics_thread.h"

namespace wiwa
{
	// The main-thread side of the physics world: turns the scene into body commands and the step poses back into Transforms. Jolt-free.
	//
	// Bodies: an entity with a RigidBody and a Collider (on itself or on a descendant that has no RigidBody of its own) is one body;
	// its colliders make one shape, a compound when there is more than one or the collider is not on the body's entity. A Collider with
	// no RigidBody on itself or above is a static body of its own. A RigidBody without any collider makes no body.
	class PhysicsSync
	{
	public:
		// Sends a CreateBody for every body of the scene to a world that was just started (forgets what an earlier world had). The
		// commands carry the world's generation, so a world started later drops them.
		void build(Scene& scene, PhysicsThread& thread);

		// Once per frame, after the scene moved: bodies whose entity was destroyed (or lost its collider) are destroyed, new ones are made,
		// changed colliders send ReplaceShape (SetMaterial when only the material asset changed), a changed layer sends SetLayer, a changed RigidBody (or
		// trigger flag) makes the body again, and static and kinematic bodies follow their
		// entity's WorldTransform (SetTransform, SetKinematicTarget).
		void push_changes(Scene& scene, PhysicsThread& thread);

		// Writes the pose of every dynamic body, interpolated between two steps by alpha (0: prev, 1: latest), to its entity's Transform
		// (converted to local through the parent; the scale is kept). WorldTransforms are not updated here. Snapshots of another world
		// generation than the one built are ignored.
		void apply_poses(Scene& scene, const PhysicsStepSnapshot& prev, const PhysicsStepSnapshot& latest, float alpha);

		// Forgets every body and joint (the world is gone).
		void clear();

		// What a Collider's Material reference means for the world, on the main thread (the physics thread never sees the AssetManager). A
		// reference that is not set gives the default material. One still loading also gives the default for now: the next push_changes sees
		// it ready and sends the shape again with the real values (SetMaterial when only the material changed, else ReplaceShape). One that
		// failed to load or does not exist gives the default and logs one warning per world.
		PhysicsMaterialData resolve_material(AssetRef ref);

		// The cooked shape of a Collider's Mesh reference (a mesh of a model imported with physics shapes). Null while it loads (the next
		// push_changes sees it ready and sends the shape again), and null with one warning per collider when the reference is not set, the
		// model has no such shape or it failed to load: the collider then collides with nothing.
		std::shared_ptr<const CookedShapeData> resolve_shape(AssetRef mesh, CookedShapeKind kind, EntityGuid collider);

		u64 generation() const { return m_Generation; }
		size_t joint_count() const { return m_Joints.size(); }
		bool has_character(BodyKey key) const { return m_Characters.contains(key); }
		size_t body_count() const { return m_Bodies.size(); }
		bool has_body(BodyKey key) const { return m_Bodies.contains(key); }

	private:
		// Sends the joints of the scene's Joint components (on entities that have a body, after the bodies were sent).
		void push_joints(const Scene& scene, PhysicsThread& thread);
		// Sends the characters of the scene's CharacterController components, and teleports the ones whose entity was moved from outside.
		void push_characters(const Scene& scene, PhysicsThread& thread);

		struct Tracked
		{
			BodyDesc Desc{};          // as last sent (Desc.Shape is the shape last sent)
			glm::mat4 LastWorld{ 1.0f }; // the body entity's WorldTransform when last sent
		};

		struct MaterialEntry
		{
			Handle<PhysicsMaterialData> Material{};
			bool Warned = false;
		};

		struct ShapeEntry
		{
			Handle<CookedShapeData> Shape{};
			std::shared_ptr<const CookedShapeData> Data{}; // a copy for the physics thread, made once the asset is ready
		};

		std::unordered_map<BodyKey, Tracked> m_Bodies{};
		std::unordered_map<AssetGuid, MaterialEntry> m_Materials{}; // by the guid the asset is stored under
		std::unordered_map<AssetGuid, ShapeEntry> m_Shapes{};           // by the guid the shape asset is stored under
		std::set<EntityGuid> m_WarnedColliders{};
		// The joints sent: a Joint component on the entity of a body gives one, named by that entity. Changes to the motor or the limits go
		// as SetJointMotor / SetJointLimits (the joint keeps its rest pose); any other change makes the joint again.
		std::unordered_map<BodyKey, JointDesc> m_Joints{};
		std::set<EntityGuid> m_WarnedJoints{};
		// The characters sent: a CharacterController on an entity that has no body of its own gives one, named by the entity. Expected is
		// the world position the entity should have if nothing but the physics moved it (what apply_poses wrote); another one is a teleport.
		struct TrackedCharacter
		{
			CharacterDesc Desc{};
			glm::vec3 Expected{ 0.0f };
		};
		std::unordered_map<BodyKey, TrackedCharacter> m_Characters{};
		std::set<EntityGuid> m_WarnedCharacters{};
		u64 m_Generation = 0;
	};
}
