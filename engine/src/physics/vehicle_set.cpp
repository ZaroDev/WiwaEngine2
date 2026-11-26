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
#include "vehicle_assists.h"

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
		std::vector<TirePeaks> Peaks{};                        // per wheel: its tire's peaks, found once (the callback runs every solver iteration)
		std::vector<float> RestLengths{};                      // per wheel: the suspension length at rest (the authored wheel centre)
		std::vector<bool> Driven{};                            // per wheel: on a driven axle
		glm::vec3 FrontAxle{ 0.0f };                           // body space: the middle of axle 0's wheels (downforce goes there)
		glm::vec3 RearAxle{ 0.0f };                            // and of the other axles' wheels
		float Wheelbase = 0.0f;
		float MaxSteer = 0.0f;                                 // radians
		JPH::BodyID Body{};
		VehicleInput Applied{};
		u32 AssistsActive = 0;
		AssistState Assist{};
		bool Reverse = false;    // automatic gearbox: driving backwards (brake pedal drives, throttle brakes)
		float ReverseHold = 0.0f; // how long brake has been held at a standstill
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

		// A wheel's spring under its static load: Jolt's FrequencyAndDamping stiffness is the body's effective mass at the force point times
		// w^2 (VehicleConstraint.cpp), so the spring sags load / (m_eff w^2). Preloaded (Jolt wants it >= 0) so that a soft spring still rests
		// in the middle of the travel; a stiff one rests above it. The authored wheel centre is the rest length below the attachment point.
		struct WheelRest
		{
			float Preload = 0.0f;
			float Length = 0.0f;
		};

		WheelRest wheel_rest(const WheelDesc& w, float sag)
		{
			WheelRest rest;
			rest.Preload = std::max(0.0f, sag - 0.5f * (w.SuspensionMaxLength - w.SuspensionMinLength));
			rest.Length = std::clamp(w.SuspensionMaxLength + rest.Preload - sag, w.SuspensionMinLength, w.SuspensionMaxLength);
			return rest;
		}

		// Each wheel's rest from the body's mass, inertia and centre of mass: the weight is shared between the front axle (0) and the others by
		// the lever of the centre of mass along z, evenly between the wheels of a group.
		std::vector<WheelRest> wheel_rests(const JPH::Body& body, const VehicleDesc& desc, float gravity)
		{
			const JPH::MotionProperties* motion = body.GetMotionProperties();
			const float inverseMass = motion->GetInverseMass();
			const float weight = inverseMass > 0.0f ? gravity / inverseMass : 0.0f;
			const float comZ = body.GetShape()->GetCenterOfMass().GetZ();
			float frontZ = 0.0f;
			float rearZ = 0.0f;
			int front = 0;
			int rear = 0;
			for (const WheelDesc& w : desc.Wheels)
			{
				(w.Axle == 0 ? frontZ : rearZ) += w.Position.z;
				(w.Axle == 0 ? front : rear)++;
			}
			float frontShare = front > 0 && rear == 0 ? 1.0f : 0.0f;
			if (front > 0 && rear > 0)
			{
				frontZ /= static_cast<float>(front);
				rearZ /= static_cast<float>(rear);
				frontShare = std::abs(rearZ - frontZ) > 1e-4f ? std::clamp((rearZ - comZ) / (rearZ - frontZ), 0.0f, 1.0f) : 0.5f;
			}
			std::vector<WheelRest> rests;
			for (const WheelDesc& w : desc.Wheels)
			{
				const float load = w.Axle == 0 ? weight * frontShare / static_cast<float>(std::max(front, 1)) : weight * (1.0f - frontShare) / static_cast<float>(std::max(rear, 1));
				const float omega = 2.0f * kPi * w.SuspensionFrequency;
				WheelRest rest = wheel_rest(w, 0.0f);
				for (int pass = 0; pass < 2; pass++) // the force point depends on the rest length a little: once more with the first answer
				{
					const JPH::Vec3 forcePoint = to_jolt(w.Position) + kUp * (rest.Length - 0.5f * (w.SuspensionMinLength + w.SuspensionMaxLength));
					const JPH::Vec3 arm = forcePoint.Cross(-kUp);
					const float effectiveMass = 1.0f / (inverseMass + arm.Dot(motion->GetLocalSpaceInverseInertia().Multiply3x3(arm)));
					rest = wheel_rest(w, load / (effectiveMass * omega * omega));
				}
				rests.push_back(rest);
			}
			return rests;
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
					&& std::isfinite(w.MaxSteerAngle) && w.MaxSteerAngle >= 0.0f && w.MaxSteerAngle < 90.0f && w.Inertia > 0.0f && w.Axle >= 0 && std::isfinite(w.Radius + w.Width + w.SuspensionMaxLength + w.SuspensionFrequency + w.SuspensionDamping + w.Inertia);
				if (!ok)
				{
					return "a wheel has a size, suspension, steer angle or inertia that is not allowed (radius, width, frequency and inertia above 0, max length above min length, steer angle 0 to 90 degrees)";
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

			const std::vector<WheelRest> rests = wheel_rests(body, desc, gravity);
			for (size_t index = 0; index < desc.Wheels.size(); index++)
			{
				const WheelDesc& w = desc.Wheels[index];
				JPH::Ref<JPH::WheelSettingsWV> wheel = new JPH::WheelSettingsWV;
				// The attachment point sits the rest length above the authored wheel centre, so the car settles with its wheels where they
				// were placed (the authored position is the ride height)
				wheel->mPosition = to_jolt(w.Position) + kUp * rests[index].Length;
				built->RestLengths.push_back(rests[index].Length);
				wheel->mSuspensionDirection = -kUp;
				wheel->mSteeringAxis = kUp;
				wheel->mWheelUp = kUp;
				wheel->mWheelForward = kForward;
				wheel->mSuspensionMinLength = w.SuspensionMinLength;
				wheel->mSuspensionMaxLength = w.SuspensionMaxLength;
				wheel->mSuspensionPreloadLength = rests[index].Preload;
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
				built->Peaks.push_back(tire_peaks(built->Tires.back()));
				bool driven = false;
				for (const VehicleDifferentialData& diff : s.Differentials)
				{
					driven = driven || diff.Axle == w.Axle;
				}
				built->Driven.push_back(driven);
				built->MaxSteer = std::max(built->MaxSteer, wheel->mMaxSteerAngle);
			}
			glm::vec3 front{ 0.0f };
			glm::vec3 rear{ 0.0f };
			int frontCount = 0;
			int rearCount = 0;
			float minZ = 0.0f;
			float maxZ = 0.0f;
			for (size_t i = 0; i < desc.Wheels.size(); i++)
			{
				const glm::vec3 p = desc.Wheels[i].Position;
				(desc.Wheels[i].Axle == 0 ? front : rear) += p;
				(desc.Wheels[i].Axle == 0 ? frontCount : rearCount)++;
				minZ = i == 0 ? p.z : std::min(minZ, p.z);
				maxZ = i == 0 ? p.z : std::max(maxZ, p.z);
			}
			built->FrontAxle = frontCount > 0 ? front / static_cast<float>(frontCount) : glm::vec3(0.0f);
			built->RearAxle = rearCount > 0 ? rear / static_cast<float>(rearCount) : built->FrontAxle;
			built->Wheelbase = maxZ - minZ;

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
				const TireLimits limits = tire_force_limits(self->Tires[wheel], self->Peaks[wheel], load, slip.Ratio, slip.Angle, longitudinalFriction);
				outLongitudinal = limits.Longitudinal * dt;
				outLateral = limits.Lateral * dt;
			});
			return built;
		}
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
		m_Controls[body].Input = input;
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
		transmission.Set(gear, transmission.GetClutchFriction()); // the auto-clutch (step) decides how much it bites
	}

	void VehicleSet::set_assist(BodyKey body, VehicleAssist assist, float strength)
	{
		if (assist < VehicleAssist::Count)
		{
			m_Controls[body].Assists[static_cast<size_t>(assist)] = std::isfinite(strength) && strength >= 0.0f ? std::min(strength, 1.0f) : -1.0f;
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
		entry.Built = build(lock.GetBody(), entry.Desc, std::max(m_System.GetGravity().Length(), 0.01f));
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
			const VehicleSettingsData& s = entry.Desc.Settings;
			const Controls& controls = m_Controls[key];
			const auto strength = [&](VehicleAssist assist, float asset)
			{
				const float value = controls.Assists[static_cast<size_t>(assist)];
				return value >= 0.0f ? value : asset;
			};
			const bool automatic = strength(VehicleAssist::AutoGearbox, s.AutomaticGearbox ? 1.0f : 0.0f) >= 0.5f;
			JPH::VehicleTransmission& transmission = v.Controller->GetTransmission();
			transmission.mMode = automatic ? JPH::ETransmissionMode::Auto : JPH::ETransmissionMode::Manual;

			const JPH::Quat rotation = bodies.GetRotation(v.Body);
			const JPH::Vec3 velocity = bodies.GetLinearVelocity(v.Body);
			const float speed = velocity.Dot(rotation * kForward);
			VehicleInput in = controls.Input;

			// automatic reverse: brake held at a standstill backs up; throttle at a standstill goes forward again (the pedals swap while backing)
			if (!automatic)
			{
				v.Reverse = false;
				v.ReverseHold = 0.0f;
			}
			else if (!v.Reverse)
			{
				// the handbrake keeps a parked car from backing up (holding the brake on a hill with it on does not reverse)
				const bool holding = std::abs(speed) < 0.5f && in.Brake > 0.1f && in.Throttle < 0.05f && in.Handbrake < 0.1f;
				v.ReverseHold = holding ? v.ReverseHold + dt : 0.0f;
				v.Reverse = v.ReverseHold >= 0.3f;
			}
			else if (in.Throttle > 0.1f && std::abs(speed) < 0.5f)
			{
				v.Reverse = false;
				v.ReverseHold = 0.0f;
			}
			if (v.Reverse)
			{
				std::swap(in.Throttle, in.Brake);
			}

			AssistInput assistIn;
			assistIn.Driver = in;
			assistIn.Speed = v.Reverse ? -speed : speed; // the assists see the direction the driver asks for as forward
			assistIn.YawRate = -bodies.GetAngularVelocity(v.Body).Dot(rotation * kUp); // + turning right: about -up
			assistIn.Wheelbase = v.Wheelbase;
			assistIn.MaxSteerAngle = v.MaxSteer;
			assistIn.Dt = dt;
			for (size_t i = 0; i < entry.Desc.Wheels.size(); i++)
			{
				const JPH::Wheel& w = *v.Constraint->GetWheel(static_cast<JPH::uint>(i));
				AssistWheelState wheel;
				wheel.SlipRatio = wheel_slip(*v.Constraint, w).Ratio * (v.Reverse ? -1.0f : 1.0f);
				wheel.PeakSlip = v.Peaks[i].Longitudinal;
				wheel.Driven = v.Driven[i];
				wheel.Brakes = entry.Desc.Wheels[i].Brakes;
				wheel.InContact = w.HasContact();
				wheel.Axle = entry.Desc.Wheels[i].Axle;
				wheel.Side = entry.Desc.Wheels[i].Side;
				assistIn.Wheels.push_back(wheel);
			}
			AssistStrengths strengths;
			strengths.TractionControl = strength(VehicleAssist::TractionControl, s.TractionControl);
			strengths.Abs = strength(VehicleAssist::Abs, s.Abs);
			strengths.Stability = strength(VehicleAssist::Stability, s.Stability);
			strengths.SteeringHelp = strength(VehicleAssist::SteeringHelp, s.SteeringHelp);
			const AssistOutput out = run_assists(assistIn, strengths, s, v.Assist);

			// per-wheel brakes: each wheel's own brake torque, with a full brake input when any wheel brakes
			float anyBrake = 0.0f;
			for (size_t i = 0; i < v.Wheels.size(); i++)
			{
				v.Wheels[i]->mMaxBrakeTorque = v.BaseBrake[i] * out.WheelBrake[i];
				anyBrake = std::max(anyBrake, out.WheelBrake[i]);
			}
			// manual gearbox: Jolt never moves the clutch itself (it keeps it closed, so the engine's idle would push the car). Auto-clutch: it
			// bites with throttle or once the wheels turn the engine above idle, and opens again below that
			if (!automatic)
			{
				const int gear = transmission.GetCurrentGear();
				float target = 0.0f;
				if (gear != 0)
				{
					const float ratio = gear > 0 ? s.Gears[static_cast<size_t>(std::min<int>(gear, static_cast<int>(s.Gears.size())) - 1)]
						: (s.ReverseGears.empty() ? 1.0f : s.ReverseGears[static_cast<size_t>(std::min<int>(-gear, static_cast<int>(s.ReverseGears.size())) - 1)]);
					const float final = s.Differentials.empty() ? 1.0f : s.Differentials[0].Ratio;
					float wheelSpeed = 0.0f;
					int driven = 0;
					for (size_t i = 0; i < v.Driven.size(); i++)
					{
						if (v.Driven[i])
						{
							wheelSpeed += std::abs(v.Constraint->GetWheel(static_cast<JPH::uint>(i))->GetAngularVelocity());
							driven++;
						}
					}
					const float rpmFromWheels = driven > 0 ? wheelSpeed / static_cast<float>(driven) * ratio * final * 60.0f / (2.0f * kPi) : 0.0f;
					target = out.Throttle > 0.02f || rpmFromWheels > 1.15f * s.MinRpm ? 1.0f : 0.0f;
				}
				const float clutch = transmission.GetClutchFriction();
				transmission.Set(gear, clutch + std::clamp(target - clutch, -4.0f * dt, 4.0f * dt));
			}

			v.Controller->SetDriverInput(v.Reverse ? -out.Throttle : out.Throttle, out.Steer, anyBrake > 0.0f ? 1.0f : 0.0f, out.Handbrake);
			v.Applied = VehicleInput{ out.Throttle, anyBrake, out.Steer, out.Handbrake };
			v.AssistsActive = out.Active;

			// aero: drag at the centre of mass, downforce split between the axles
			constexpr float kAirDensity = 1.225f;
			const float speedSq = velocity.LengthSq();
			if (speedSq > 1e-4f)
			{
				bodies.AddForce(v.Body, -0.5f * kAirDensity * s.DragArea * std::sqrt(speedSq) * velocity);
				const float downforce = 0.5f * kAirDensity * s.DownforceArea * speed * speed;
				if (downforce > 0.0f)
				{
					const JPH::RMat44 transform = bodies.GetWorldTransform(v.Body);
					const JPH::Vec3 down = -(rotation * kUp);
					bodies.AddForce(v.Body, down * downforce * s.DownforceFront, transform * to_jolt(v.FrontAxle));
					bodies.AddForce(v.Body, down * downforce * (1.0f - s.DownforceFront), transform * to_jolt(v.RearAxle));
				}
			}

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
			pose.AssistsActive = v.AssistsActive;
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
				wheel.RestLength = v.RestLengths[i];
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
					wheel.AligningTorque = tire_forces(v.Tires[i], v.Peaks[i], wheel.Load, slip.Ratio, slip.Angle, wheel.Grip).AligningTorque;
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
		m_Controls.clear();
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
