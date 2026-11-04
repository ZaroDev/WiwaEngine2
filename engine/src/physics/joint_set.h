#pragma once

#include "jolt_config.h"
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <functional>
#include <unordered_map>

#include "physics_commands.h"

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

		// How many joints are made right now.
		u32 made_count() const;

	private:
		struct Entry
		{
			JointDesc Desc{};
			JPH::Ref<JPH::TwoBodyConstraint> Constraint{};
			bool Failed = false; // cannot be built as described; not tried again
		};

		void try_make(Entry& entry);
		void unmake(Entry& entry);

		JPH::PhysicsSystem& m_System;
		BodyLookup m_Lookup;
		std::unordered_map<BodyKey, Entry> m_Joints{};
	};
}
