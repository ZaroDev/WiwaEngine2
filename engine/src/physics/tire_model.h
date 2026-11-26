#pragma once

#include <math/math.h>

namespace wiwa
{
	// One Pacejka "magic formula" curve: y = D sin(C atan(B x - E (B x - atan(B x)))). D is the peak friction coefficient at the nominal
	// load; B, C and E shape the curve (stiffness, shape, curvature).
	struct TireCurve
	{
		float B = 10.0f;
		float C = 1.6f;
		float D = 1.0f;
		float E = 0.0f;
		bool operator==(const TireCurve&) const = default;
	};

	// A tire as the vehicles' tire model sees it (vehicles.md). Plain data and Jolt-free.
	struct TireModel
	{
		TireCurve Longitudinal{ 12.0f, 1.65f, 1.0f, 0.0f }; // x: slip ratio
		TireCurve Lateral{ 10.0f, 1.35f, 1.0f, -0.5f };     // x: slip angle, radians
		float NominalLoad = 4000.0f;   // N (Fz0)
		float LoadSensitivity = 0.15f; // fraction of mu lost per NominalLoad above nominal (gained below)
		float PneumaticTrail = 0.03f;  // m
		bool operator==(const TireModel&) const = default;
	};

	// What a tire produces at a slip: N, N, Nm. The forces have the signs of the slips; the aligning torque turns the wheel back
	// toward the direction of travel.
	struct TireForces
	{
		float Longitudinal = 0.0f;
		float Lateral = 0.0f;
		float AligningTorque = 0.0f;
	};

	// The most force the tire can give right now in each direction (N, at least 0): the cap Jolt's friction constraint gets.
	struct TireLimits
	{
		float Longitudinal = 0.0f;
		float Lateral = 0.0f;
	};

	float magic_formula(const TireCurve& curve, float x);
	// The x > 0 of the curve's first maximum.
	float magic_formula_peak_x(const TireCurve& curve);
	// How the peak friction scales with the load: 1 - LoadSensitivity * (load - Fz0) / Fz0, clamped to [0.5, 1.5].
	float tire_load_scale(const TireModel& tire, float load);
	// The forces at a slip ratio and a slip angle (radians), combined through the normalised slip vector (a friction ellipse). grip is the
	// surface's factor (PhysicsMaterial TireGrip). A load of 0 or less, or no slip, gives zeros.
	TireForces tire_forces(const TireModel& tire, float load, float slipRatio, float slipAngle, float grip);
	// The available force: below the peak the whole ellipse (what the other direction's use leaves of it), past the peak the curve's
	// falloff. Continuous at the peak; full grip at zero slip.
	TireLimits tire_force_limits(const TireModel& tire, float load, float slipRatio, float slipAngle, float grip);
	// Finite values, B, C, D > 0, NominalLoad > 0, LoadSensitivity in [0, 1), PneumaticTrail >= 0.
	bool tire_model_valid(const TireModel& tire);
}
