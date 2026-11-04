#pragma once

#include "jolt_config.h"
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <functional>
#include <set>
#include <vector>
#include <unordered_map>

#include "physics_commands.h"
#include "physics_snapshot.h"

namespace wiwa
{
	// The joint table of one physics world, next to its body table. Physics thread only. A joint is named by the entity that owns it; it is
	// made (a Jolt constraint exists) while both its bodies exist, and waits unmade otherwise: before its second body appears, and after
	// one of them is destroyed (a body made again under the same key takes its joints back). A description that cannot be built is
	// logged once and kept unmade until it is replaced.
	class JointSet
	{
	public:
		// The Jolt body of a key (invalid when the key has no body).
		using BodyLookup = std::function<JPH::BodyID(BodyKey)>;

		JointSet(JPH::PhysicsSystem& system, BodyLookup lookup) : m_System(system), m_Lookup(std::move(lookup)) {}
		~JointSet() { clear(); }

		JointSet(const JointSet&) = delete;
		JointSet& operator=(const JointSet&) = delete;

		// Makes the joint of desc.A (replacing that owner's earlier one) as soon as both bodies exist.
		void create(const JointDesc& desc);
		void destroy(BodyKey owner);
		void set_motor(BodyKey owner, JointMotorMode mode, float target, float maxForce);
		void set_limits(BodyKey owner, float min, float max);

		// A body was made: the waiting joints that name it are made if their other body exists.
		void body_created(BodyKey key);
		// A body is about to be destroyed: the joints that name it are unmade (they wait). Called before the body goes.
		void body_destroyed(BodyKey key);

		// Unmakes every joint and forgets them (before the bodies are destroyed).
		void clear();

		// After a step of dt seconds: a joint that pushed with more than its break force or torque is unmade for good (it is made again
		// only by a new CreateJoint) and gives a JointBreak event (A the owner, B the other body or a null key for the world).
		void check_breaks(float dt, std::vector<PhysicsEvent>& events);

		// The body pairs of the made joints that have CollideConnected off (see ContactCollector::NoCollidePairs).
		const std::set<std::pair<JPH::uint32, JPH::uint32>>& no_collide_pairs() const { return m_NoCollide; }

		// How many joints are made right now.
		u32 made_count() const;

	private:
		struct Entry
		{
			JointDesc Desc{};
			JPH::Ref<JPH::TwoBodyConstraint> Constraint{};
			bool Failed = false; // cannot be built as described, or broke; not tried again
			std::pair<JPH::uint32, JPH::uint32> NoCollide{}; // the pair this joint keeps from colliding while it is made ((0, 0): none)
		};

		void try_make(Entry& entry);
		void unmake(Entry& entry);

		JPH::PhysicsSystem& m_System;
		BodyLookup m_Lookup;
		std::unordered_map<BodyKey, Entry> m_Joints{};
		std::set<std::pair<JPH::uint32, JPH::uint32>> m_NoCollide{};
	};
}
