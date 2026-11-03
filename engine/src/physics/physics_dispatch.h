#pragma once

#include <functional>
#include <span>
#include <vector>

#include "physics_snapshot.h"

namespace wiwa
{
	// Where the completed steps go on the main thread: the script instances in the engine, a recorder in a test.
	class IPhysicsStepSink
	{
	public:
		virtual ~IPhysicsStepSink() = default;
		// The step's contact and trigger events, in the step's order. Before on_physics_update of the same step.
		virtual void on_step_events(const PhysicsStepSnapshot& step, const std::vector<PhysicsEvent>& events) = 0;
		// Once per step (dt is the fixed step), unless the step was skipped (see dispatch_physics_steps).
		virtual void on_physics_update(const PhysicsStepSnapshot& step, float dt) = 0;
	};

	// When the main thread falls behind, only this many of the latest steps get on_physics_update; the older ones deliver their events
	// but are otherwise skipped. A step is 1/60 s, so a frame of 250 ms is still delivered whole.
	constexpr u32 PHYSICS_MAX_DISPATCHED_STEPS = 32;

	struct PhysicsDispatchResult
	{
		u32 Updated = 0; // steps that got on_physics_update
		u32 Skipped = 0; // steps that only delivered their events
	};

	// Delivers the steps, oldest first: for each its events (filtered by `events_of`, which gives the events that still concern entities
	// that exist), then on_physics_update. Begin and end events (enters, exits) are never dropped; Stay events of skipped steps are.
	// While a step is delivered `current_query` is that step's query snapshot, so that a handler's queries see the step it is called for.
	PhysicsDispatchResult dispatch_physics_steps(std::span<const Ref<const PhysicsStepSnapshot>> steps,
		const std::function<std::vector<PhysicsEvent>(const PhysicsStepSnapshot&)>& events_of, IPhysicsStepSink& sink, Ref<const QuerySnapshot>& current_query);
}
