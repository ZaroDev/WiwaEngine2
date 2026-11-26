#include <editor_common.h>
#include "vehicle_checks.h"

#include <cmath>

#include <asset_pipeline/physics_material_file.h>
#include <assets/wasset_io.h>
#include <physics/physics_material_asset.h>
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

		// TireGrip on physics materials: the JSON key, the payload round trip, and a version 1 payload (no TireGrip) loading with 1.
		void check_physmat_tire_grip(Check& check)
		{
			PhysicsMaterialData parsed;
			std::string parseError;
			check(physics_material_from_text("{ \"tireGrip\": 0.6 }", parsed, parseError) && parsed.TireGrip == 0.6f, "physmat_tire_grip_json: \"tireGrip\" is read");
			check(physics_material_from_text("{}", parsed, parseError) && parsed.TireGrip == 1.0f, "physmat_tire_grip_json: a missing \"tireGrip\" is 1");
			check(!physics_material_from_text("{ \"tireGrip\": -1 }", parsed, parseError), "physmat_tire_grip_json: a negative \"tireGrip\" is refused");
			check(physics_material_to_json(parsed).contains("tireGrip"), "physmat_tire_grip_json: written back");
			PhysicsMaterialData grippy = default_physics_material();
			grippy.TireGrip = 0.6f;
			const std::optional<PhysicsMaterialData> back = deserialize<PhysicsMaterialData>(serialize(grippy));
			check(back && back->TireGrip == 0.6f, "physmat_tire_grip_roundtrip: the payload keeps TireGrip");
			BinaryArchive v1 = BinaryArchive::writer();
			u32 version1 = 1;
			float friction = 0.7f, restitution = 0.2f, density = 900.0f;
			CombineMode combine = CombineMode::Max;
			v1.value(version1);
			v1.value(friction);
			v1.value(restitution);
			v1.value(density);
			v1.value(combine);
			v1.value(combine);
			const std::vector<std::byte> v1Bytes = v1.take();
			const std::optional<PhysicsMaterialData> old = deserialize<PhysicsMaterialData>(v1Bytes);
			check(old && old->Friction == 0.7f && old->Density == 900.0f && old->TireGrip == 1.0f, "physmat_v1_payload: a version 1 payload loads with TireGrip 1");
		}
	}

	void run_vehicle_checks(Check& check)
	{
		check_tire_model(check);
		check_physmat_tire_grip(check);
	}
}
