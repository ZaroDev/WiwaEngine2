#pragma once

#include <unordered_map>

#include <scene/scene.h>

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
		// changed colliders send ReplaceShape, a changed RigidBody makes the body again, and static and kinematic bodies follow their
		// entity's WorldTransform (SetTransform, SetKinematicTarget).
		void push_changes(Scene& scene, PhysicsThread& thread);

		// Writes the pose of every dynamic body, interpolated between two steps by alpha (0: prev, 1: latest), to its entity's Transform
		// (converted to local through the parent; the scale is kept). WorldTransforms are not updated here. Snapshots of another world
		// generation than the one built are ignored.
		void apply_poses(Scene& scene, const PhysicsStepSnapshot& prev, const PhysicsStepSnapshot& latest, float alpha);

		// Forgets every body (the world is gone).
		void clear();

		u64 generation() const { return m_Generation; }
		size_t body_count() const { return m_Bodies.size(); }
		bool has_body(BodyKey key) const { return m_Bodies.contains(key); }

	private:
		struct Tracked
		{
			BodyDesc Desc{};          // as last sent (Desc.Shape is the shape last sent)
			glm::mat4 LastWorld{ 1.0f }; // the body entity's WorldTransform when last sent
		};

		std::unordered_map<BodyKey, Tracked> m_Bodies{};
		u64 m_Generation = 0;
	};
}
