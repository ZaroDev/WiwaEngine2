#pragma once

#include <vector>

#include "physics_types.h"

namespace wiwa
{
	// The pose of one body after a step.
	struct BodyPose
	{
		BodyKey Key{};
		glm::vec3 Position{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 LinearVelocity{ 0.0f };
		glm::vec3 AngularVelocity{ 0.0f };
		bool Active = false; // awake (not sleeping)
	};

	// A contact or trigger event of one step. Filled in by the contact listener (a later task).
	struct PhysicsEvent
	{
	};

	// What the physics thread publishes after each step. Immutable once published; the main thread never reads Jolt structures.
	struct PhysicsStepSnapshot
	{
		u64 World = 0;       // the generation of the world that published it (PhysicsThread::generation); a new world never shows an older one's
		u64 StepIndex = 0;   // steps completed since the world started (0: the world before its first step)
		double Time = 0.0;   // simulated seconds since the world started
		float FixedDt = 0.0f; // the length of the step that produced this snapshot
		std::vector<BodyPose> Poses{};
		std::vector<PhysicsEvent> Events{};
	};
}
