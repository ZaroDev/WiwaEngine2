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

		// Forgets every body (the world is gone).
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
		size_t body_count() const { return m_Bodies.size(); }
		bool has_body(BodyKey key) const { return m_Bodies.contains(key); }

	private:
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
		u64 m_Generation = 0;
	};
}
