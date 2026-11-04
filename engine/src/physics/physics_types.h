#pragma once

#include <math/math.h>
#include <scene/entity_guid.h>

namespace wiwa
{
	// A body is named by the entity it belongs to.
	using BodyKey = EntityGuid;

	enum class MotionType : u32 { Static, Kinematic, Dynamic };

	// Only append: the values are saved (Joint component).
	enum class JointType : u32 { Fixed, Point, Hinge, Slider, Distance, Cone, SwingTwist, SixDof };
	enum class JointMotorMode : u32 { Off, Velocity, Position };

	// The per-scene step settings. Plain data: copied to the physics thread through a SetSettings command.
	struct PhysicsSettings
	{
		glm::vec3 Gravity{ 0, -9.81f, 0 };
		u32 FixedRateHz = 60;     // steps per second of simulated time
		u32 MaxStepsPerWake = 4;  // a cap on catch-up steps; past it the simulation falls behind real time instead of stalling
		u32 VelocityIterations = 10;
		u32 PositionIterations = 2;
		float SleepThreshold = 0.03f; // m/s below which a body may go to sleep

		bool operator==(const PhysicsSettings&) const = default;
	};
}
