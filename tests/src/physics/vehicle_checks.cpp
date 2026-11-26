#include <editor_common.h>
#include "vehicle_checks.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <thread>

#include <asset_pipeline/asset_database.h>
#include <asset_pipeline/physics_material_file.h>
#include <asset_pipeline/vehicle_file.h>
#include <assets/asset_manager.h>
#include <core/engine.h>
#include <assets/wasset_io.h>
#include <physics/physics_material_asset.h>
#include <physics/tire_model.h>
#include <physics/vehicle_settings.h>

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

		// RigidBody::CenterOfMassOffset: a tall box tipped 30 degrees rights itself with its mass low, and falls over without.
		float upright_after_tip(glm::vec3 comOffset)
		{
			SyncWorld world;
			if (!world.start())
			{
				return -2.0f;
			}
			world.send(CreateBody{ EntityGuid::generate(), box_body(MotionType::Static, glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(40.0f, 0.5f, 40.0f)) });
			const BodyKey tall = EntityGuid::generate();
			BodyDesc desc = box_body(MotionType::Dynamic, glm::vec3(0.0f, 1.2f, 0.0f), glm::vec3(0.3f, 1.0f, 0.3f));
			desc.Rotation = glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 0.0f, 1.0f));
			desc.CenterOfMassOffset = comOffset;
			world.send(CreateBody{ tall, desc });
			const PhysicsStepSnapshot after = world.step(360);
			const BodyPose* pose = find_pose(&after, tall);
			return pose ? glm::dot(pose->Rotation * glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f)) : -2.0f;
		}

		void check_body_com_offset(Check& check)
		{
			const float low = upright_after_tip(glm::vec3(0.0f, -0.9f, 0.0f));
			const float centred = upright_after_tip(glm::vec3(0.0f));
			check(low > 0.95f, "body_com_offset: a tall box with its mass low rights itself, up.y " + std::to_string(low));
			check(centred < 0.3f, "body_com_offset: the same box with its mass centred falls over, up.y " + std::to_string(centred));
		}

		// The .wvehicle format: JSON round trip, defaults for missing keys, refusals naming the key, the binary payload, and a file
		// imported through the pipeline loading through the AssetManager.
		void check_vehicle_asset(Check& check, AssetPipelineModule& pipeline)
		{
			const VehicleSettingsData defaults = default_vehicle_settings();
			check(vehicle_settings_valid(defaults), "vehicle_defaults_valid: the default settings are valid");
			VehicleSettingsData parsed;
			std::string error;
			check(vehicle_from_text(vehicle_to_json(defaults).dump(2), parsed, error) && parsed == defaults, "vehicle_json_roundtrip: the default settings survive JSON, " + error);
			VehicleSettingsData tuned = defaults;
			tuned.RearTire.Lateral.D = 1.3f;
			tuned.Gears = { 3.0f, 2.0f };
			tuned.AutomaticGearbox = false;
			tuned.SteerLimit = { { 0.0f, 1.0f }, { 20.0f, 0.5f } };
			check(vehicle_from_text(vehicle_to_json(tuned).dump(), parsed, error) && parsed == tuned, "vehicle_json_roundtrip: changed values survive JSON, " + error);
			check(vehicle_from_text("{ \"version\": 1 }", parsed, error) && parsed == defaults, "vehicle_json_defaults: missing keys take the defaults, " + error);
			for (const auto& [name, text] : std::vector<std::pair<std::string, std::string>>{
				{ "unsorted_curve", "{ \"version\": 1, \"engine\": { \"torqueCurve\": [[5000, 300], [1000, 200]] } }" },
				{ "zero_gear", "{ \"version\": 1, \"transmission\": { \"gears\": [3.0, 0.0] } }" },
				{ "assist_range", "{ \"version\": 1, \"assists\": { \"abs\": 2 } }" },
				{ "wrong_type", "{ \"version\": 1, \"aero\": { \"dragArea\": \"big\" } }" },
				{ "not_json", "{ \"version\": " } })
			{
				error.clear();
				check(!vehicle_from_text(text, parsed, error) && !error.empty(), "vehicle_json_rejects: " + name + " is refused with a reason");
			}
			const std::optional<VehicleSettingsData> back = deserialize<VehicleSettingsData>(serialize(tuned));
			check(back && *back == tuned, "vehicle_payload_roundtrip: the binary payload keeps every value");
			std::vector<std::byte> wrongVersion = serialize(tuned);
			wrongVersion[0] = std::byte{ 99 };
			check(!deserialize<VehicleSettingsData>(wrongVersion).has_value(), "vehicle_payload_version: an unknown version is refused");

			AssetDatabase& database = pipeline.get_database();
			const std::filesystem::path dir = database.get_assets_root() / "zz_vehicle_smoke";
			std::error_code fsError;
			std::filesystem::remove_all(dir, fsError);
			std::filesystem::create_directories(dir, fsError);
			{
				std::ofstream out(dir / "car.wvehicle", std::ios::binary);
				out << vehicle_to_json(tuned).dump(2);
			}
			const std::optional<AssetRef> ref = database.ensure_imported(dir / "car.wvehicle");
			check(ref.has_value(), "vehicle_cook_load: the .wvehicle imports");
			AssetManager* assets = Engine::get().get_module<AssetManager>();
			if (ref && assets != nullptr)
			{
				const Handle<VehicleSettingsData> handle = assets->load<VehicleSettingsData>(*ref);
				const auto giveUp = std::chrono::steady_clock::now() + std::chrono::seconds(5);
				while (std::chrono::steady_clock::now() < giveUp && handle.state() == LoadState::Loading)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				}
				check(handle.state() == LoadState::Ready && handle.get() != nullptr && *handle.get() == tuned, "vehicle_cook_load: the cooked vehicle loads with the values of the file");
			}
			std::filesystem::remove_all(dir, fsError);
			database.scan();
		}
	}

	void run_vehicle_checks(Check& check, AssetPipelineModule& pipeline)
	{
		check_tire_model(check);
		check_physmat_tire_grip(check);
		check_body_com_offset(check);
		check_vehicle_asset(check, pipeline);
	}
}
