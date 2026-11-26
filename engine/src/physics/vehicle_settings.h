#pragma once

#include <string>
#include <vector>

#include <math/math.h>

#include "tire_model.h"

namespace wiwa
{
	// A point of an engine torque curve: Nm at an rpm.
	struct TorquePoint
	{
		float Rpm = 0.0f;
		float Torque = 0.0f;
		bool operator==(const TorquePoint&) const = default;
	};

	// A driven axle: its Left and Right wheels (VehicleWheel Axle and Side) share the engine torque through a differential.
	struct VehicleDifferentialData
	{
		i32 Axle = 1;
		float Ratio = 3.42f;            // the final drive of this axle
		float LimitedSlipRatio = 1.4f;  // the most one wheel may turn faster than the other (above 1)
		float EngineTorqueRatio = 1.0f; // the share of the engine torque this axle gets (the shares are normalised)
		bool operator==(const VehicleDifferentialData&) const = default;
	};

	// How much of MaxSteerAngle steering may use at a speed (with SteeringHelp 1).
	struct SteerLimitPoint
	{
		float Speed = 0.0f;    // m/s
		float Fraction = 1.0f; // 0..1
		bool operator==(const SteerLimitPoint&) const = default;
	};

	// The tuning of a vehicle: the Vehicle asset (.wvehicle) as it is cooked. Plain data; the physics thread gets a copy (vehicles.md).
	struct VehicleSettingsData
	{
		// engine
		std::vector<TorquePoint> TorqueCurve{ { 1000.0f, 250.0f }, { 4500.0f, 400.0f }, { 7000.0f, 300.0f } };
		float MinRpm = 1000.0f;
		float MaxRpm = 7000.0f;
		float EngineInertia = 0.2f; // kg m2 (the flywheel and crank of a road car)
		float EngineDamping = 0.2f;
		// transmission
		bool AutomaticGearbox = true;
		std::vector<float> Gears{ 3.2f, 2.1f, 1.5f, 1.15f, 0.9f, 0.75f };
		std::vector<float> ReverseGears{ 3.0f };
		float ShiftUpRpm = 6200.0f;
		float ShiftDownRpm = 2500.0f;
		float ShiftTime = 0.25f; // s
		float ClutchStrength = 10.0f;
		// drive and roll
		std::vector<VehicleDifferentialData> Differentials{ VehicleDifferentialData{} }; // rear-wheel drive
		std::vector<float> AntiRollStiffness{ 8000.0f, 5000.0f };                        // N/m, index = axle
		// tires: axle 0 uses the front tire, every other axle the rear
		TireModel FrontTire{};
		TireModel RearTire{};
		// aero
		float DragArea = 0.7f;        // Cd * A, m2
		float DownforceArea = 0.3f;   // Cl * A, m2
		float DownforceFront = 0.45f; // the share of the downforce on the front axle
		// brakes
		float MaxBrakeTorque = 2500.0f; // Nm per wheel at full brake
		float HandbrakeTorque = 4000.0f;
		float BrakeBiasFront = 0.6f;    // 0.5: even
		// assists, 0..1
		float TractionControl = 1.0f;
		float Abs = 1.0f;
		float Stability = 1.0f;
		float SteeringHelp = 1.0f;
		// steering
		float SteerSpeed = 3.0f; // steer input units per second at SteeringHelp 1
		std::vector<SteerLimitPoint> SteerLimit{ { 0.0f, 1.0f }, { 30.0f, 0.3f } };

		bool operator==(const VehicleSettingsData&) const = default;
	};

	constexpr u32 VEHICLE_SETTINGS_VERSION = 1;

	VehicleSettingsData default_vehicle_settings();
	// Finite values; at least one torque point, sorted by rpm, torques >= 0; MinRpm < MaxRpm; at least one forward gear, every ratio > 0;
	// shift rpms and clutch strength above 0, ShiftUpRpm above ShiftDownRpm (and below MaxRpm for an automatic gearbox); differential axles
	// >= 0 with positive ratios, limited slip ratios above 1 and some engine torque shared; assists, DownforceFront and BrakeBiasFront in [0, 1];
	// both tires valid; the steer limit sorted by speed with fractions in [0, 1]. why (when given) says what is wrong.
	bool vehicle_settings_valid(const VehicleSettingsData& data, std::string* why = nullptr);
	// The steer fraction allowed at a speed (linear between the points, the end values outside; 1 without points).
	float steer_limit_at(const VehicleSettingsData& data, float speed);
}
