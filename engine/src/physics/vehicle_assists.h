#pragma once

#include <vector>

#include "physics_commands.h"
#include "physics_snapshot.h"

namespace wiwa
{
	// The driver assists of a vehicle (vehicles.md): pure functions, Jolt-free, run on the physics thread before each step.

	struct AssistWheelState
	{
		float SlipRatio = 0.0f; // signed (+: spinning, -: locking)
		float PeakSlip = 0.085f; // the slip ratio at which the wheel's tire gives the most
		bool Driven = false;
		bool Brakes = false;
		bool InContact = false;
		i32 Axle = 0;
		WheelSide Side = WheelSide::Left;
	};

	struct AssistInput
	{
		VehicleInput Driver{};
		float Speed = 0.0f;         // m/s along forward, signed
		float YawRate = 0.0f;       // rad/s, + turning right
		float Wheelbase = 2.6f;     // m
		float MaxSteerAngle = 0.6f; // radians: the largest of the steering wheels
		float Dt = 1.0f / 120.0f;
		std::vector<AssistWheelState> Wheels{};
	};

	struct AssistStrengths
	{
		float TractionControl = 1.0f;
		float Abs = 1.0f;
		float Stability = 1.0f;
		float SteeringHelp = 1.0f;
	};

	struct AssistOutput
	{
		float Throttle = 0.0f;
		float Steer = 0.0f;
		float Handbrake = 0.0f;
		std::vector<float> WheelBrake{}; // 0..1 of each wheel's brake torque
		u32 Active = 0;                  // VehicleAssistActive bits
	};

	// What carries over between steps: the rate-limited steer, and how much traction control is cutting (it rises at once and eases off
	// slowly, so a wheel that spun up in one step does not get full throttle back in the next).
	struct AssistState
	{
		float Steer = 0.0f;
		float TcCut = 0.0f;
	};

	// Steering help: the input capped by the speed (SteerLimit) and rate-limited (SteerSpeed), both scaled by the strength (0: the input as it
	// is). ABS: a braked wheel sliding past its tire's peak gets its brake eased. Traction control: a driven wheel spinning past its peak cuts
	// the throttle and brakes that wheel. Stability: a yaw rate away from what the steer asks for (a bicycle model, capped by the grip)
	// brakes the outer front wheel (oversteer) or the inner rear (understeer) and eases the throttle.
	AssistOutput run_assists(const AssistInput& in, const AssistStrengths& strengths, const VehicleSettingsData& settings, AssistState& state);
}
