#pragma once

#include "jolt_config.h"
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>

#include <string>

#include "physics_commands.h"

namespace wiwa
{
	// All the Jolt knowledge of joints: the constraint a JointDesc describes. The constraint is made in world space from the two bodies'
	// poses as they are now, so the pose they have when the joint is made is its rest pose (hinge and slider limits count from it, which is
	// why they are limited to min <= 0 <= max). Anchors are given from the bodies' origins (entity positions), not their centres of mass.
	// The constraint's first body is `b` and its second is `a`, so angles and slider positions are those of `a` relative to `b` (a positive
	// motor target turns or moves the owner `a` the right-handed way around, or along, the axis). `b` is Body::sFixedToWorld for a joint to the world. Null with `error` set when the description cannot be built (a zero or non-finite
	// axis, min > max, a number that is not finite).
	JPH::Ref<JPH::TwoBodyConstraint> build_joint(const JointDesc& desc, JPH::Body& a, JPH::Body& b, std::string& error);

	// What a joint pushed with during the last step, as total impulses: Linear in N s, Angular in N m s (divide by the step for the force
	// and the torque). Used for break limits.
	struct JointReaction
	{
		float Linear = 0.0f;
		float Angular = 0.0f;
	};
	JointReaction joint_reaction(const JPH::TwoBodyConstraint& constraint, JointType type);

	// Hinge and slider joints have motors, the others ignore the call. maxForce <= 0 means no limit. Hinge angles are degrees, slider
	// positions metres.
	void apply_joint_motor(JPH::TwoBodyConstraint& constraint, JointType type, JointMotorMode mode, float target, float maxForce);

	// Hinge limits in degrees (clamped to min <= 0 <= max, within 180), slider and distance limits in metres. Both 0: no limit.
	void apply_joint_limits(JPH::TwoBodyConstraint& constraint, JointType type, float min, float max);
}
