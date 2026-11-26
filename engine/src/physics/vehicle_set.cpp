#include <pch.h>
#include "vehicle_set.h"

#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>

#include <algorithm>
#include <cmath>
#include <numbers>

#include "physics_material_jolt.h"
#include "tire_model.h"

namespace wiwa
{
	// What a made vehicle holds on the physics thread. Heap-allocated so the Jolt callbacks can keep a pointer to it.
	struct BuiltVehicle
	{
		JPH::Ref<JPH::VehicleConstraint> Constraint{};
		JPH::WheeledVehicleController* Controller = nullptr;
		std::vector<JPH::Ref<JPH::WheelSettingsWV>> Wheels{}; // the settings Jolt reads every step (brake torque is set per wheel)
		std::vector<TireModel> Tires{};                        // per wheel
		std::vector<float> BaseBrake{};                        // MaxBrakeTorque per wheel, from the settings and the bias
		std::vector<float> Grip{};                             // per wheel, written by the combine function
		JPH::BodyID Body{};
		VehicleInput Applied{};
	};

	namespace
	{
		constexpr float kPi = std::numbers::pi_v<float>;
		const JPH::Vec3 kUp(0.0f, 1.0f, 0.0f);
		const JPH::Vec3 kForward(0.0f, 0.0f, -1.0f);

		JPH::Vec3 to_jolt(glm::vec3 v) { return JPH::Vec3(v.x, v.y, v.z); }
		glm::vec3 to_glm(JPH::Vec3 v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }

		bool finite3(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

		// The signed slips of a wheel from its contact (zeros without one). The ground speed is floored at 1 m/s so a standstill stays finite.
		struct WheelSlip
		{
			float Ratio = 0.0f;
			float Angle = 0.0f;
		};

		WheelSlip wheel_slip(const JPH::VehicleConstraint& constraint, const JPH::Wheel& wheel)
		{
			WheelSlip slip;
			if (!wheel.HasContact())
			{
				return slip;
			}
			JPH::Vec3 relative = constraint.GetVehicleBody()->GetPointVelocity(wheel.GetContactPosition()) - wheel.GetContactPointVelocity();
			const JPH::Vec3 normal = wheel.GetContactNormal();
			relative -= normal.Dot(relative) * normal;
			const float longitudinal = relative.Dot(wheel.GetContactLongitudinal());
			const float lateral = relative.Dot(wheel.GetContactLateral());
			const float ground = std::max(std::abs(longitudinal), 1.0f);
			slip.Ratio = (wheel.GetAngularVelocity() * wheel.GetSettings()->mRadius - longitudinal) / ground;
			slip.Angle = std::atan2(lateral, ground);
			return slip;
		}

		// The spring sags g / w^2 under its share of the weight (a FrequencyAndDamping spring). Preloaded (Jolt wants it >= 0) so that a soft
		// spring still rests in the middle of the travel; a stiff one rests above it.
		float wheel_preload(const WheelDesc& w, float gravity)
		{
			const float omega = 2.0f * kPi * w.SuspensionFrequency;
			const float sag = gravity / (omega * omega);
			return std::max(0.0f, sag - 0.5f * (w.SuspensionMaxLength - w.SuspensionMinLength));
		}

		// Why a description cannot be made (empty: it can).
		std::string unbuildable(const VehicleDesc& desc)
		{
			std::string why;
			if (!vehicle_settings_valid(desc.Settings, &why))
			{
				return "its settings are not valid: " + why;
			}
			if (desc.Wheels.empty())
			{
				return "it has no wheels";
			}
			for (const WheelDesc& w : desc.Wheels)
			{
				const bool ok = finite3(w.Position) && w.Radius > 0.0f && w.Width > 0.0f && w.SuspensionMinLength >= 0.0f
					&& w.SuspensionMaxLength > w.SuspensionMinLength && w.SuspensionFrequency > 0.0f && w.SuspensionDamping >= 0.0f
					&& std::isfinite(w.MaxSteerAngle) && w.Inertia > 0.0f && w.Axle >= 0 && std::isfinite(w.Radius + w.Width + w.SuspensionMaxLength + w.SuspensionFrequency + w.SuspensionDamping + w.Inertia);
				if (!ok)
				{
					return "a wheel has a size, suspension or inertia that is not allowed (radius, width, frequency and inertia above 0, max length above min length)";
				}
			}
			for (const VehicleDifferentialData& diff : desc.Settings.Differentials)
			{
				int left = 0;
				int right = 0;
				for (const WheelDesc& w : desc.Wheels)
				{
					left += w.Axle == diff.Axle && w.Side == WheelSide::Left ? 1 : 0;
					right += w.Axle == diff.Axle && w.Side == WheelSide::Right ? 1 : 0;
				}
				if (left != 1 || right != 1)
				{
					return "driven axle " + std::to_string(diff.Axle) + " needs exactly one Left and one Right wheel";
				}
			}
			return {};
		}

		int wheel_index(const VehicleDesc& desc, i32 axle, WheelSide side)
		{
			for (size_t i = 0; i < desc.Wheels.size(); i++)
			{
				if (desc.Wheels[i].Axle == axle && desc.Wheels[i].Side == side)
				{
					return static_cast<int>(i);
				}
			}
			return -1;
		}

		std::unique_ptr<BuiltVehicle> build(JPH::Body& body, const VehicleDesc& desc, float gravity)
		{
			const VehicleSettingsData& s = desc.Settings;
			auto built = std::make_unique<BuiltVehicle>();
			built->Body = body.GetID();

			JPH::VehicleConstraintSettings settings;
			settings.mUp = kUp;
			settings.mForward = kForward;

			for (const WheelDesc& w : desc.Wheels)
			{
				JPH::Ref<JPH::WheelSettingsWV> wheel = new JPH::WheelSettingsWV;
				// The attachment point sits the rest length above the authored wheel centre, so the car settles with its wheels where they
				// were placed (the authored position is the ride height)
				wheel->mPosition = to_jolt(w.Position) + kUp * wheel_rest_length(w, gravity);
				wheel->mSuspensionDirection = -kUp;
				wheel->mSteeringAxis = kUp;
				wheel->mWheelUp = kUp;
				wheel->mWheelForward = kForward;
				wheel->mSuspensionMinLength = w.SuspensionMinLength;
				wheel->mSuspensionMaxLength = w.SuspensionMaxLength;
				wheel->mSuspensionPreloadLength = wheel_preload(w, gravity);
				wheel->mSuspensionSpring = JPH::SpringSettings(JPH::ESpringMode::FrequencyAndDamping, w.SuspensionFrequency, w.SuspensionDamping);
				wheel->mRadius = w.Radius;
				wheel->mWidth = w.Width;
				wheel->mInertia = w.Inertia;
				wheel->mMaxSteerAngle = w.Steers ? glm::radians(w.MaxSteerAngle) : 0.0f;
				// Flat curves: the tire impulse callback decides the grip from our tire model
				wheel->mLongitudinalFriction.Clear();
				wheel->mLongitudinalFriction.AddPoint(0.0f, 1.0f);
				wheel->mLongitudinalFriction.AddPoint(1000.0f, 1.0f);
				wheel->mLateralFriction.Clear();
				wheel->mLateralFriction.AddPoint(0.0f, 1.0f);
				wheel->mLateralFriction.AddPoint(1000.0f, 1.0f);
				const float bias = w.Axle == 0 ? s.BrakeBiasFront : 1.0f - s.BrakeBiasFront;
				const float brake = w.Brakes ? s.MaxBrakeTorque * 2.0f * bias : 0.0f;
				wheel->mMaxBrakeTorque = brake;
				wheel->mMaxHandBrakeTorque = w.Handbrake ? s.HandbrakeTorque : 0.0f;
				settings.mWheels.push_back(JPH::Ref<JPH::WheelSettings>(wheel.GetPtr()));
				built->Wheels.push_back(wheel);
				built->Tires.push_back(w.Axle == 0 ? s.FrontTire : s.RearTire);
				built->BaseBrake.push_back(brake);
				built->Grip.push_back(0.0f);
			}

			for (size_t axle = 0; axle < s.AntiRollStiffness.size(); axle++)
			{
				const int left = wheel_index(desc, static_cast<i32>(axle), WheelSide::Left);
				const int right = wheel_index(desc, static_cast<i32>(axle), WheelSide::Right);
				if (left >= 0 && right >= 0 && s.AntiRollStiffness[axle] > 0.0f)
				{
					JPH::VehicleAntiRollBar bar;
					bar.mLeftWheel = left;
					bar.mRightWheel = right;
					bar.mStiffness = s.AntiRollStiffness[axle];
					settings.mAntiRollBars.push_back(bar);
				}
			}

			JPH::Ref<JPH::WheeledVehicleControllerSettings> controller = new JPH::WheeledVehicleControllerSettings;
			float maxTorque = 0.0f;
			for (const TorquePoint& p : s.TorqueCurve)
			{
				maxTorque = std::max(maxTorque, p.Torque);
			}
			controller->mEngine.mMaxTorque = maxTorque;
			controller->mEngine.mMinRPM = s.MinRpm;
			controller->mEngine.mMaxRPM = s.MaxRpm;
			controller->mEngine.mNormalizedTorque.Clear();
			for (const TorquePoint& p : s.TorqueCurve)
			{
				// Jolt reads the curve at current rpm / max rpm
				controller->mEngine.mNormalizedTorque.AddPoint(p.Rpm / s.MaxRpm, maxTorque > 0.0f ? p.Torque / maxTorque : 0.0f);
			}
			controller->mEngine.mInertia = s.EngineInertia;
			controller->mEngine.mAngularDamping = s.EngineDamping;
			controller->mTransmission.mMode = s.AutomaticGearbox ? JPH::ETransmissionMode::Auto : JPH::ETransmissionMode::Manual;
			controller->mTransmission.mGearRatios.clear();
			for (const float ratio : s.Gears)
			{
				controller->mTransmission.mGearRatios.push_back(ratio);
			}
			controller->mTransmission.mReverseGearRatios.clear();
			for (const float ratio : s.ReverseGears)
			{
				controller->mTransmission.mReverseGearRatios.push_back(-ratio); // Jolt's reverse ratios are negative
			}
			controller->mTransmission.mSwitchTime = s.ShiftTime;
			controller->mTransmission.mShiftUpRPM = s.ShiftUpRpm;
			controller->mTransmission.mShiftDownRPM = s.ShiftDownRpm;
			controller->mTransmission.mClutchStrength = s.ClutchStrength;
			float torqueShares = 0.0f;
			for (const VehicleDifferentialData& diff : s.Differentials)
			{
				torqueShares += diff.EngineTorqueRatio;
			}
			for (const VehicleDifferentialData& diff : s.Differentials)
			{
				JPH::VehicleDifferentialSettings d;
				d.mLeftWheel = wheel_index(desc, diff.Axle, WheelSide::Left);
				d.mRightWheel = wheel_index(desc, diff.Axle, WheelSide::Right);
				d.mDifferentialRatio = diff.Ratio;
				d.mLimitedSlipRatio = diff.LimitedSlipRatio;
				d.mEngineTorqueRatio = torqueShares > 0.0f ? diff.EngineTorqueRatio / torqueShares : 0.0f;
				controller->mDifferentials.push_back(d);
			}
			settings.mController = controller;

			built->Constraint = new JPH::VehicleConstraint(body, settings);
			built->Constraint->SetVehicleCollisionTester(new JPH::VehicleCollisionTesterCastCylinder(body.GetObjectLayer()));
			built->Controller = static_cast<JPH::WheeledVehicleController*>(built->Constraint->GetController());

			BuiltVehicle* self = built.get();
			// The constraint's own friction combine (not the world's): the grip of the surface the wheel touches
			built->Constraint->SetCombineFriction([self](JPH::uint wheel, float& longitudinal, float& lateral, const JPH::Body& other, const JPH::SubShapeID& subShape)
			{
				const float grip = material_of(other, subShape).TireGrip;
				if (wheel < self->Grip.size())
				{
					self->Grip[wheel] = grip;
				}
				longitudinal = grip;
				lateral = grip;
			});
			// The cap Jolt's friction constraints get: the force the tire can still give (tire_force_limits), never the force at the current slip
			built->Controller->SetTireMaxImpulseCallback([self](JPH::uint wheel, float& outLongitudinal, float& outLateral, float suspensionImpulse,
				float longitudinalFriction, float, float, float, float dt)
			{
				const JPH::Wheel* w = self->Constraint->GetWheel(wheel);
				const WheelSlip slip = wheel_slip(*self->Constraint, *w);
				const float load = dt > 0.0f ? std::max(suspensionImpulse, 0.0f) / dt : 0.0f;
				const TireLimits limits = tire_force_limits(self->Tires[wheel], load, slip.Ratio, slip.Angle, longitudinalFriction);
				outLongitudinal = limits.Longitudinal * dt;
				outLateral = limits.Lateral * dt;
			});
			return built;
		}
	}

	float wheel_rest_length(const WheelDesc& w, float gravity)
	{
		const float omega = 2.0f * kPi * w.SuspensionFrequency;
		const float sag = gravity / (omega * omega);
		return std::clamp(w.SuspensionMaxLength + wheel_preload(w, gravity) - sag, w.SuspensionMinLength, w.SuspensionMaxLength);
	}

	VehicleSet::VehicleSet(JPH::PhysicsSystem& system, BodyLookup body_of, KeyLookup key_of) : m_System(system), m_BodyOf(std::move(body_of)), m_KeyOf(std::move(key_of)) {}

	VehicleSet::~VehicleSet() { clear(); }

	void VehicleSet::create(BodyKey body, const VehicleDesc& desc)
	{
		Entry& entry = m_Vehicles[body];
		unmake(entry);
		entry.Desc = desc;
		entry.Unbuildable = false;
		const std::string why = unbuildable(desc);
		if (!why.empty())
		{
			entry.Unbuildable = true;
			WI_PHYSICS_LOG(Warning, "The vehicle on {0} cannot be made: {1}", body.to_string(), why);
			return;
		}
		make(body, entry);
	}

	void VehicleSet::destroy(BodyKey body)
	{
		const auto it = m_Vehicles.find(body);
		if (it != m_Vehicles.end())
		{
			unmake(it->second);
			m_Vehicles.erase(it);
		}
	}

	void VehicleSet::set_input(BodyKey body, const VehicleInput& input)
	{
		const auto it = m_Vehicles.find(body);
		if (it != m_Vehicles.end())
		{
			it->second.Input = input;
		}
	}

	void VehicleSet::shift(BodyKey body, i32 direction)
	{
		const auto it = m_Vehicles.find(body);
		if (it == m_Vehicles.end() || it->second.Built == nullptr)
		{
			return;
		}
		JPH::VehicleTransmission& transmission = it->second.Built->Controller->GetTransmission();
		if (transmission.mMode != JPH::ETransmissionMode::Manual)
		{
			return;
		}
		const VehicleSettingsData& s = it->second.Desc.Settings;
		const int gear = std::clamp(transmission.GetCurrentGear() + (direction > 0 ? 1 : -1), -static_cast<int>(s.ReverseGears.size()), static_cast<int>(s.Gears.size()));
		transmission.Set(gear, 1.0f);
	}

	void VehicleSet::set_assist(BodyKey body, VehicleAssist assist, float strength)
	{
		const auto it = m_Vehicles.find(body);
		if (it != m_Vehicles.end() && assist < VehicleAssist::Count)
		{
			it->second.Assists[static_cast<size_t>(assist)] = std::isfinite(strength) && strength >= 0.0f ? std::min(strength, 1.0f) : -1.0f;
		}
	}

	void VehicleSet::body_destroyed(BodyKey key)
	{
		const auto it = m_Vehicles.find(key);
		if (it != m_Vehicles.end())
		{
			unmake(it->second);
		}
	}

	void VehicleSet::make(BodyKey key, Entry& entry)
	{
		if (entry.Built != nullptr || entry.Unbuildable)
		{
			return;
		}
		const JPH::BodyID id = m_BodyOf(key);
		if (id.IsInvalid())
		{
			return;
		}
		JPH::BodyLockWrite lock(m_System.GetBodyLockInterfaceNoLock(), id);
		if (!lock.Succeeded() || !lock.GetBody().IsDynamic())
		{
			return; // waits: a body whose shape has not arrived yet is not dynamic for now
		}
		m_Gravity = std::max(m_System.GetGravity().Length(), 0.01f);
		entry.Built = build(lock.GetBody(), entry.Desc, m_Gravity);
		m_System.AddConstraint(entry.Built->Constraint);
		m_System.AddStepListener(entry.Built->Constraint);
	}

	void VehicleSet::unmake(Entry& entry)
	{
		if (entry.Built == nullptr)
		{
			return;
		}
		m_System.RemoveStepListener(entry.Built->Constraint);
		m_System.RemoveConstraint(entry.Built->Constraint);
		entry.Built.reset();
	}

	void VehicleSet::step(float dt)
	{
		m_LastDt = dt;
		JPH::BodyInterface& bodies = m_System.GetBodyInterfaceNoLock();
		for (auto& [key, entry] : m_Vehicles)
		{
			make(key, entry);
			if (entry.Built == nullptr)
			{
				continue;
			}
			BuiltVehicle& v = *entry.Built;
			const VehicleInput in = entry.Input;
			v.Applied = in;
			v.Controller->SetDriverInput(std::clamp(in.Throttle, 0.0f, 1.0f), std::clamp(in.Steer, -1.0f, 1.0f), std::clamp(in.Brake, 0.0f, 1.0f),
				std::clamp(in.Handbrake, 0.0f, 1.0f));
			const bool driving = in.Throttle != 0.0f || in.Brake != 0.0f || in.Steer != 0.0f || in.Handbrake != 0.0f;
			if (driving && !bodies.IsActive(v.Body))
			{
				bodies.ActivateBody(v.Body);
			}
		}
	}

	void VehicleSet::write(PhysicsStepSnapshot& snapshot) const
	{
		const JPH::BodyInterface& bodies = m_System.GetBodyInterfaceNoLock();
		for (const auto& [key, entry] : m_Vehicles)
		{
			if (entry.Built == nullptr)
			{
				continue;
			}
			const BuiltVehicle& v = *entry.Built;
			VehiclePose pose;
			pose.Body = key;
			const glm::vec3 velocity = to_glm(bodies.GetLinearVelocity(v.Body));
			const glm::vec3 forward = to_glm(bodies.GetRotation(v.Body) * kForward);
			pose.Speed = glm::dot(velocity, forward);
			pose.EngineRpm = v.Controller->GetEngine().GetCurrentRPM();
			pose.Gear = v.Controller->GetTransmission().GetCurrentGear();
			pose.Clutch = v.Controller->GetTransmission().GetClutchFriction();
			pose.Applied = v.Applied;
			const JPH::VehicleConstraint& constraint = *v.Constraint;
			for (size_t i = 0; i < entry.Desc.Wheels.size(); i++)
			{
				const JPH::Wheel& w = *constraint.GetWheel(static_cast<JPH::uint>(i));
				WheelPose wheel;
				wheel.Entity = entry.Desc.Wheels[i].Entity;
				wheel.SteerAngle = w.GetSteerAngle();
				float spin = w.GetRotationAngle();
				wheel.SpinAngle = spin > kPi ? spin - 2.0f * kPi : spin;
				wheel.SuspensionLength = w.GetSuspensionLength();
				wheel.RestLength = wheel_rest_length(entry.Desc.Wheels[i], m_Gravity);
				wheel.InContact = w.HasContact();
				wheel.AngularVelocity = w.GetAngularVelocity();
				if (wheel.InContact)
				{
					wheel.ContactBody = m_KeyOf(w.GetContactBodyID());
					wheel.Grip = v.Grip[i];
					const WheelSlip slip = wheel_slip(constraint, w);
					wheel.SlipRatio = slip.Ratio;
					wheel.SlipAngle = slip.Angle;
					wheel.Load = std::max(w.GetSuspensionLambda(), 0.0f) / m_LastDt;
					wheel.AligningTorque = tire_forces(v.Tires[i], wheel.Load, slip.Ratio, slip.Angle, wheel.Grip).AligningTorque;
				}
				pose.Wheels.push_back(wheel);
			}
			snapshot.Vehicles.push_back(std::move(pose));
		}
		snapshot.VehicleCount = made_count();
	}

	void VehicleSet::clear()
	{
		for (auto& [key, entry] : m_Vehicles)
		{
			unmake(entry);
		}
		m_Vehicles.clear();
	}

	u32 VehicleSet::made_count() const
	{
		u32 count = 0;
		for (const auto& [key, entry] : m_Vehicles)
		{
			count += entry.Built != nullptr ? 1 : 0;
		}
		return count;
	}
}
