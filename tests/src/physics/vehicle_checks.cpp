#include <editor_common.h>
#include "vehicle_checks.h"

#include <cmath>

#include <physics/tire_model.h>

namespace wiwa
{
	namespace
	{
		using namespace physics_test;

		// The tire model alone: the magic formula's peak, load sensitivity, combined slip, signs, and the limits Jolt's friction gets.
		void check_tire_model(Check& check)
		{
			TireModel t;
			const float load = t.NominalLoad;
			const float peak = magic_formula_peak_x(t.Lateral);
			check(peak > 0.02f && peak < 0.4f, "tire_peak_lateral: peak slip angle in a plausible range, got " + std::to_string(peak));
			check(magic_formula(t.Lateral, peak) >= magic_formula(t.Lateral, peak * 0.8f) && magic_formula(t.Lateral, peak) >= magic_formula(t.Lateral, peak * 1.5f), "tire_peak_is_max");
			const float muLow = tire_forces(t, load * 0.5f, 0.0f, peak, 1.0f).Lateral / (load * 0.5f);
			const float muHigh = tire_forces(t, load * 2.0f, 0.0f, peak, 1.0f).Lateral / (load * 2.0f);
			check(muHigh < muLow, "tire_load_sensitivity: mu falls with load");
			const float pure = tire_forces(t, load, 0.0f, peak, 1.0f).Lateral;
			const float combined = tire_forces(t, load, -0.1f, peak, 1.0f).Lateral;
			check(std::abs(combined) < std::abs(pure) * 0.9f, "tire_combined_slip: braking costs lateral force");
			check(tire_forces(t, load, 0.0f, -peak, 1.0f).Lateral < 0.0f && tire_forces(t, load, 0.05f, 0.0f, 1.0f).Longitudinal > 0.0f, "tire_signs");
			check(std::abs(tire_forces(t, load, 0.0f, peak, 0.5f).Lateral - 0.5f * pure) < 1.0f, "tire_grip_scales");
			check(tire_forces(t, 0.0f, 0.1f, 0.1f, 1.0f).Lateral == 0.0f && tire_forces(t, load, 0.0f, 0.0f, 1.0f).Longitudinal == 0.0f, "tire_zero_cases");
			check(tire_forces(t, load, 0.0f, 0.02f, 1.0f).AligningTorque < 0.0f && std::abs(tire_forces(t, load, 0.0f, peak * 3.0f, 1.0f).AligningTorque) < 1e-3f, "tire_aligning_torque: restoring below the peak, gone past it");
			TireModel bad;
			bad.NominalLoad = 0.0f;
			check(!tire_model_valid(bad) && tire_model_valid(t), "tire_valid_rejects");

			const TireLimits rest = tire_force_limits(t, load, 0.0f, 0.0f, 1.0f);
			check(std::abs(rest.Lateral - load * t.Lateral.D) < 1.0f && std::abs(rest.Longitudinal - load * t.Longitudinal.D) < 1.0f, "tire_limits_static: full grip at zero slip");
			check(tire_force_limits(t, load, magic_formula_peak_x(t.Longitudinal) * 0.9f, 0.0f, 1.0f).Lateral < rest.Lateral * 0.5f, "tire_limits_combined: braking near the peak leaves little lateral grip");
			check(tire_force_limits(t, load, 0.0f, peak * 4.0f, 1.0f).Lateral < tire_force_limits(t, load, 0.0f, peak, 1.0f).Lateral, "tire_limits_falloff: sliding past the peak gives less");
			const TireLimits below = tire_force_limits(t, load, 0.0f, peak * 0.999f, 1.0f);
			const TireLimits above = tire_force_limits(t, load, 0.0f, peak * 1.001f, 1.0f);
			check(std::abs(below.Lateral - above.Lateral) < load * 0.01f, "tire_limits_continuous: no jump at the peak");
		}
	}

	void run_vehicle_checks(Check& check)
	{
		check_tire_model(check);
	}
}
