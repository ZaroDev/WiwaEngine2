#include <pch.h>
#include "physics_dispatch.h"

#include <algorithm>

namespace wiwa
{
	PhysicsDispatchResult dispatch_physics_steps(std::span<const Ref<const PhysicsStepSnapshot>> steps,
		const std::function<std::vector<PhysicsEvent>(const PhysicsStepSnapshot&)>& events_of, IPhysicsStepSink& sink, Ref<const QuerySnapshot>& current_query)
	{
		PhysicsDispatchResult result;
		const size_t firstUpdated = steps.size() > PHYSICS_MAX_DISPATCHED_STEPS ? steps.size() - PHYSICS_MAX_DISPATCHED_STEPS : 0;

		// the query snapshot goes back to what it was, whatever the sink does
		struct Restore
		{
			Ref<const QuerySnapshot>& Target;
			Ref<const QuerySnapshot> Saved;
			~Restore() { Target = std::move(Saved); }
		} restore{ current_query, current_query };

		for (size_t i = 0; i < steps.size(); i++)
		{
			const PhysicsStepSnapshot& step = *steps[i];
			const bool skipped = i < firstUpdated;
			current_query = step.Query;

			std::vector<PhysicsEvent> events = events_of(step);
			if (skipped)
			{
				std::erase_if(events, [](const PhysicsEvent& event) { return event.Kind == PhysicsEventKind::CollisionStay; });
			}
			if (!events.empty())
			{
				sink.on_step_events(step, events);
			}

			if (skipped)
			{
				result.Skipped++;
			}
			else
			{
				sink.on_physics_update(step, step.FixedDt);
				result.Updated++;
			}
		}
		return result;
	}
}
