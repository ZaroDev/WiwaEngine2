#pragma once

#include "physics_commands.h"
#include "physics_layers.h"
#include "physics_snapshot.h"

namespace wiwa
{
	// One Jolt world: the physics system, its job system and temp allocator, the body table (BodyKey to Jolt body) and the shape builder.
	// Made, used and destroyed on the physics thread only, so nothing in it locks. The Jolt types stay in physics_world.cpp; the
	// thread sees this interface. Jolt's process-wide runtime (allocator, factory, types) must be set up while a world exists.
	class IPhysicsWorld
	{
	public:
		virtual ~IPhysicsWorld() = default;

		// Applies one command (an unknown key is ignored).
		virtual void execute(const PhysicsCommand& command) = 0;
		// Advances the world by one fixed step.
		virtual void step() = 0;
		// Fills the poses of every body after a step.
		virtual void write_poses(PhysicsStepSnapshot& snapshot) const = 0;
		// Moves the contact and trigger events made since the last call (the commands' and the step's) into the snapshot, sorted (see
		// PhysicsStepSnapshot::Events).
		virtual void take_events(PhysicsStepSnapshot& snapshot) = 0;
		// Fills the snapshot's query data (see QuerySnapshot).
		virtual void write_query(PhysicsStepSnapshot& snapshot) = 0;
		// Fills the snapshot's debug drawing when SetDebugDraw asked for it.
		virtual void write_debug(PhysicsStepSnapshot& snapshot) = 0;

		virtual double fixed_dt() const = 0;
		virtual u32 max_steps_per_wake() const = 0;
	};

	// May throw (out of memory); the thread reports that as a failed start.
	Scope<IPhysicsWorld> create_physics_world(const PhysicsSettings& settings, const PhysicsLayers& layers);
}
