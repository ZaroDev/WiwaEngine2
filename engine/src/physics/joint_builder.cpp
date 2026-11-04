#include <pch.h>
#include "joint_builder.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include <Jolt/Physics/Constraints/ConeConstraint.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>

namespace wiwa
{
	namespace
	{
		constexpr float kDegrees = JPH::JPH_PI / 180.0f;

		bool finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
		JPH::Vec3 to_jolt(glm::vec3 v) { return JPH::Vec3(v.x, v.y, v.z); }
		float radians(float degrees) { return degrees * kDegrees; }

		// A unit vector from a vector given by the user; false for zero and non-finite ones.
		bool direction(glm::vec3 v, JPH::Vec3& out)
		{
			if (!finite(v) || glm::length(v) < 1e-6f)
			{
				return false;
			}
			out = to_jolt(v).Normalized();
			return true;
		}

		JPH::SpringSettings spring_of(const JointDesc& desc)
		{
			return JPH::SpringSettings(JPH::ESpringMode::FrequencyAndDamping, desc.SpringFrequency, std::max(desc.SpringDamping, 0.0f));
		}

		bool valid_numbers(const JointDesc& desc, std::string& error)
		{
			const float numbers[] = { desc.LimitMin, desc.LimitMax, desc.SwingHalfAngle, desc.TwistHalfAngle, desc.SpringFrequency, desc.SpringDamping,
				desc.MotorTarget, desc.MotorMaxForce, desc.BreakForce, desc.BreakTorque };
			for (const float number : numbers)
			{
				if (!std::isfinite(number))
				{
					error = "a number of the joint is not finite";
					return false;
				}
			}
			if (!finite(desc.AnchorA) || !finite(desc.AnchorB))
			{
				error = "an anchor of the joint is not finite";
				return false;
			}
			if (desc.LimitMin > desc.LimitMax)
			{
				error = "the joint's LimitMin is greater than its LimitMax";
				return false;
			}
			return true;
		}

		void set_hinge_limits(JPH::HingeConstraintSettings& settings, float min, float max)
		{
			if (min != 0.0f || max != 0.0f)
			{
				settings.mLimitsMin = std::clamp(radians(min), -JPH::JPH_PI, 0.0f);
				settings.mLimitsMax = std::clamp(radians(max), 0.0f, JPH::JPH_PI);
			}
		}

		void set_slider_limits(JPH::SliderConstraintSettings& settings, float min, float max)
		{
			if (min != 0.0f || max != 0.0f)
			{
				settings.mLimitsMin = std::min(min, 0.0f);
				settings.mLimitsMax = std::max(max, 0.0f);
			}
		}
	}

	JPH::Ref<JPH::TwoBodyConstraint> build_joint(const JointDesc& desc, JPH::Body& a, JPH::Body& b, std::string& error)
	{
		if (!valid_numbers(desc, error))
		{
			return nullptr;
		}
		JPH::Vec3 axisLocal;
		JPH::Vec3 normalLocal;
		if (!direction(desc.Axis, axisLocal))
		{
			error = "the joint's Axis is zero or not finite";
			return nullptr;
		}
		if (!direction(desc.NormalAxis, normalLocal))
		{
			normalLocal = axisLocal.GetNormalizedPerpendicular();
		}

		const bool toWorld = &b == &JPH::Body::sFixedToWorld;
		const JPH::RVec3 pointA = a.GetPosition() + JPH::RVec3(a.GetRotation() * to_jolt(desc.AnchorA));
		const JPH::RVec3 pointB = toWorld ? JPH::RVec3(to_jolt(desc.AnchorB)) : b.GetPosition() + JPH::RVec3(b.GetRotation() * to_jolt(desc.AnchorB));
		const JPH::Vec3 axis = (a.GetRotation() * axisLocal).Normalized();
		JPH::Vec3 normal = a.GetRotation() * normalLocal;
		normal -= axis * axis.Dot(normal);
		normal = normal.Length() < 1e-4f ? axis.GetNormalizedPerpendicular() : normal.Normalized();

		switch (desc.Type)
		{
		case JointType::Fixed:
		{
			JPH::FixedConstraintSettings settings;
			settings.mSpace = JPH::EConstraintSpace::WorldSpace;
			settings.mPoint1 = pointB;
			settings.mPoint2 = pointA;
			return settings.Create(b, a);
		}
		case JointType::Point:
		{
			JPH::PointConstraintSettings settings;
			settings.mSpace = JPH::EConstraintSpace::WorldSpace;
			settings.mPoint1 = pointB;
			settings.mPoint2 = pointA;
			return settings.Create(b, a);
		}
		case JointType::Hinge:
		{
			JPH::HingeConstraintSettings settings;
			settings.mSpace = JPH::EConstraintSpace::WorldSpace;
			settings.mPoint1 = pointB;
			settings.mPoint2 = pointA;
			settings.mHingeAxis1 = settings.mHingeAxis2 = axis;
			settings.mNormalAxis1 = settings.mNormalAxis2 = normal;
			set_hinge_limits(settings, desc.LimitMin, desc.LimitMax);
			if (desc.SpringFrequency > 0.0f)
			{
				settings.mLimitsSpringSettings = spring_of(desc);
			}
			JPH::Ref<JPH::TwoBodyConstraint> constraint = settings.Create(b, a);
			apply_joint_motor(*constraint, desc.Type, desc.MotorMode, desc.MotorTarget, desc.MotorMaxForce);
			return constraint;
		}
		case JointType::Slider:
		{
			JPH::SliderConstraintSettings settings;
			settings.mSpace = JPH::EConstraintSpace::WorldSpace;
			settings.mPoint1 = pointB;
			settings.mPoint2 = pointA;
			settings.mSliderAxis1 = settings.mSliderAxis2 = axis;
			settings.mNormalAxis1 = settings.mNormalAxis2 = normal;
			set_slider_limits(settings, desc.LimitMin, desc.LimitMax);
			if (desc.SpringFrequency > 0.0f)
			{
				settings.mLimitsSpringSettings = spring_of(desc);
			}
			JPH::Ref<JPH::TwoBodyConstraint> constraint = settings.Create(b, a);
			apply_joint_motor(*constraint, desc.Type, desc.MotorMode, desc.MotorTarget, desc.MotorMaxForce);
			return constraint;
		}
		case JointType::Distance:
		{
			JPH::DistanceConstraintSettings settings;
			settings.mSpace = JPH::EConstraintSpace::WorldSpace;
			settings.mPoint1 = pointB;
			settings.mPoint2 = pointA;
			if (desc.LimitMin != 0.0f || desc.LimitMax != 0.0f)
			{
				settings.mMinDistance = std::max(desc.LimitMin, 0.0f);
				settings.mMaxDistance = std::max(desc.LimitMax, settings.mMinDistance);
			}
			if (desc.SpringFrequency > 0.0f)
			{
				settings.mLimitsSpringSettings = spring_of(desc);
			}
			return settings.Create(b, a);
		}
		case JointType::Cone:
		{
			JPH::ConeConstraintSettings settings;
			settings.mSpace = JPH::EConstraintSpace::WorldSpace;
			settings.mPoint1 = pointB;
			settings.mPoint2 = pointA;
			settings.mTwistAxis1 = settings.mTwistAxis2 = axis;
			settings.mHalfConeAngle = std::clamp(radians(desc.SwingHalfAngle), 0.0f, JPH::JPH_PI);
			return settings.Create(b, a);
		}
		case JointType::SwingTwist:
		{
			JPH::SwingTwistConstraintSettings settings;
			settings.mSpace = JPH::EConstraintSpace::WorldSpace;
			settings.mPosition1 = pointB;
			settings.mPosition2 = pointA;
			settings.mTwistAxis1 = settings.mTwistAxis2 = axis;
			settings.mPlaneAxis1 = settings.mPlaneAxis2 = normal;
			const float swing = std::clamp(radians(desc.SwingHalfAngle), 0.0f, JPH::JPH_PI);
			const float twist = std::clamp(radians(desc.TwistHalfAngle), 0.0f, JPH::JPH_PI);
			settings.mNormalHalfConeAngle = swing;
			settings.mPlaneHalfConeAngle = swing;
			settings.mTwistMinAngle = -twist;
			settings.mTwistMaxAngle = twist;
			return settings.Create(b, a);
		}
		case JointType::SixDof:
		{
			// translation locked; turning limited around the Axis (twist) and around the two others (swing)
			JPH::SixDOFConstraintSettings settings;
			settings.mSpace = JPH::EConstraintSpace::WorldSpace;
			settings.mPosition1 = pointB;
			settings.mPosition2 = pointA;
			settings.mAxisX1 = settings.mAxisX2 = axis;
			settings.mAxisY1 = settings.mAxisY2 = normal;
			settings.MakeFixedAxis(JPH::SixDOFConstraintSettings::EAxis::TranslationX);
			settings.MakeFixedAxis(JPH::SixDOFConstraintSettings::EAxis::TranslationY);
			settings.MakeFixedAxis(JPH::SixDOFConstraintSettings::EAxis::TranslationZ);
			const float swing = std::clamp(radians(desc.SwingHalfAngle), 0.0f, JPH::JPH_PI);
			const float twist = std::clamp(radians(desc.TwistHalfAngle), 0.0f, JPH::JPH_PI);
			settings.SetLimitedAxis(JPH::SixDOFConstraintSettings::EAxis::RotationX, -twist, twist);
			settings.SetLimitedAxis(JPH::SixDOFConstraintSettings::EAxis::RotationY, -swing, swing);
			settings.SetLimitedAxis(JPH::SixDOFConstraintSettings::EAxis::RotationZ, -swing, swing);
			return settings.Create(b, a);
		}
		}
		error = "unknown joint type";
		return nullptr;
	}

	JointReaction joint_reaction(const JPH::TwoBodyConstraint& constraint, JointType type)
	{
		JointReaction reaction;
		switch (type)
		{
		case JointType::Fixed:
		{
			const auto& c = static_cast<const JPH::FixedConstraint&>(constraint);
			reaction.Linear = c.GetTotalLambdaPosition().Length();
			reaction.Angular = c.GetTotalLambdaRotation().Length();
			break;
		}
		case JointType::Point:
			reaction.Linear = static_cast<const JPH::PointConstraint&>(constraint).GetTotalLambdaPosition().Length();
			break;
		case JointType::Hinge:
		{
			const auto& c = static_cast<const JPH::HingeConstraint&>(constraint);
			reaction.Linear = c.GetTotalLambdaPosition().Length();
			reaction.Angular = c.GetTotalLambdaRotation().Length() + std::abs(c.GetTotalLambdaRotationLimits());
			break;
		}
		case JointType::Slider:
		{
			const auto& c = static_cast<const JPH::SliderConstraint&>(constraint);
			reaction.Linear = c.GetTotalLambdaPosition().Length() + std::abs(c.GetTotalLambdaPositionLimits());
			reaction.Angular = c.GetTotalLambdaRotation().Length();
			break;
		}
		case JointType::Distance:
			reaction.Linear = std::abs(static_cast<const JPH::DistanceConstraint&>(constraint).GetTotalLambdaPosition());
			break;
		case JointType::Cone:
		{
			const auto& c = static_cast<const JPH::ConeConstraint&>(constraint);
			reaction.Linear = c.GetTotalLambdaPosition().Length();
			reaction.Angular = std::abs(c.GetTotalLambdaRotation());
			break;
		}
		case JointType::SwingTwist:
		{
			const auto& c = static_cast<const JPH::SwingTwistConstraint&>(constraint);
			reaction.Linear = c.GetTotalLambdaPosition().Length();
			reaction.Angular = JPH::Vec3(c.GetTotalLambdaTwist(), c.GetTotalLambdaSwingY(), c.GetTotalLambdaSwingZ()).Length();
			break;
		}
		case JointType::SixDof:
		{
			const auto& c = static_cast<const JPH::SixDOFConstraint&>(constraint);
			reaction.Linear = c.GetTotalLambdaPosition().Length();
			reaction.Angular = c.GetTotalLambdaRotation().Length();
			break;
		}
		}
		return reaction;
	}

	void apply_joint_motor(JPH::TwoBodyConstraint& constraint, JointType type, JointMotorMode mode, float target, float maxForce)
	{
		const float limit = maxForce > 0.0f && std::isfinite(maxForce) ? maxForce : FLT_MAX;
		const JPH::EMotorState state = mode == JointMotorMode::Velocity ? JPH::EMotorState::Velocity : mode == JointMotorMode::Position ? JPH::EMotorState::Position : JPH::EMotorState::Off;
		if (type == JointType::Hinge)
		{
			auto& hinge = static_cast<JPH::HingeConstraint&>(constraint);
			hinge.GetMotorSettings().SetTorqueLimit(limit);
			hinge.SetTargetAngularVelocity(target);
			hinge.SetTargetAngle(radians(target));
			hinge.SetMotorState(state);
		}
		else if (type == JointType::Slider)
		{
			auto& slider = static_cast<JPH::SliderConstraint&>(constraint);
			slider.GetMotorSettings().SetForceLimit(limit);
			slider.SetTargetVelocity(target);
			slider.SetTargetPosition(target);
			slider.SetMotorState(state);
		}
	}

	void apply_joint_limits(JPH::TwoBodyConstraint& constraint, JointType type, float min, float max)
	{
		const bool unlimited = min == 0.0f && max == 0.0f;
		if (type == JointType::Hinge)
		{
			static_cast<JPH::HingeConstraint&>(constraint).SetLimits(unlimited ? -JPH::JPH_PI : std::clamp(radians(min), -JPH::JPH_PI, 0.0f), unlimited ? JPH::JPH_PI : std::clamp(radians(max), 0.0f, JPH::JPH_PI));
		}
		else if (type == JointType::Slider)
		{
			static_cast<JPH::SliderConstraint&>(constraint).SetLimits(unlimited ? -FLT_MAX : std::min(min, 0.0f), unlimited ? FLT_MAX : std::max(max, 0.0f));
		}
		else if (type == JointType::Distance && !unlimited)
		{
			const float lowest = std::max(min, 0.0f);
			static_cast<JPH::DistanceConstraint&>(constraint).SetDistance(lowest, std::max(max, lowest));
		}
	}
}
