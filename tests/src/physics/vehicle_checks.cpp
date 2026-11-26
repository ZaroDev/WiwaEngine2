#include <editor_common.h>
#include "vehicle_checks.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <thread>
#include <tuple>

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

		// ---- vehicles on a world stepped on this thread at 120 Hz ----

		constexpr u32 kHz = 120;
		constexpr float kRideHeight = 0.63f; // the test car's body origin above the floor at rest: wheel centres 0.3 below it, radius 0.33

		bool finite_vec(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

		glm::vec3 forward_of(const BodyPose& pose) { return pose.Rotation * glm::vec3(0.0f, 0.0f, -1.0f); }

		bool all_finite(const VehiclePose& v)
		{
			bool ok = std::isfinite(v.Speed) && std::isfinite(v.EngineRpm) && std::isfinite(v.Clutch);
			for (const WheelPose& w : v.Wheels)
			{
				ok = ok && std::isfinite(w.SteerAngle) && std::isfinite(w.SpinAngle) && std::isfinite(w.SuspensionLength) && std::isfinite(w.SlipRatio)
					&& std::isfinite(w.SlipAngle) && std::isfinite(w.Load) && std::isfinite(w.AligningTorque) && std::isfinite(w.AngularVelocity);
			}
			return ok;
		}

		// A static floor, 400 m square, top at y = 0, tilted about x by `tiltDegrees`, with that material.
		BodyKey make_vehicle_floor(SyncWorld& world, PhysicsMaterialData material = default_physics_material(), float tiltDegrees = 0.0f)
		{
			const BodyKey floor = EntityGuid::generate();
			const glm::quat tilt = glm::angleAxis(glm::radians(tiltDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
			BodyDesc desc = box_body(MotionType::Static, tilt * glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(200.0f, 0.5f, 200.0f));
			desc.Rotation = tilt;
			desc.Shape.Material = material;
			world.send(CreateBody{ floor, desc });
			return floor;
		}

		// Without assists (they come with their own checks) the default 400 Nm car spins its rear wheels in first gear; these checks of the
		// drivetrain, brakes and steering use a 200 Nm engine that stays within the rear tires' grip.
		VehicleSettingsData tame_settings()
		{
			VehicleSettingsData s = default_vehicle_settings();
			s.TorqueCurve = { { 1000.0f, 150.0f }, { 4500.0f, 200.0f }, { 7000.0f, 170.0f } };
			return s;
		}

		VehicleDesc test_car_desc(const VehicleSettingsData& settings = tame_settings())
		{
			VehicleDesc desc;
			desc.Settings = settings;
			for (const auto& [x, z, axle] : { std::tuple{ -0.8f, -1.3f, 0 }, std::tuple{ 0.8f, -1.3f, 0 }, std::tuple{ -0.8f, 1.3f, 1 }, std::tuple{ 0.8f, 1.3f, 1 } })
			{
				WheelDesc wheel;
				wheel.Entity = EntityGuid::generate();
				wheel.Position = glm::vec3(x, -0.3f, z);
				wheel.Axle = axle;
				wheel.Side = x < 0.0f ? WheelSide::Left : WheelSide::Right;
				wheel.Steers = axle == 0;
				wheel.Handbrake = axle == 1;
				desc.Wheels.push_back(wheel);
			}
			return desc;
		}

		// A 1200 kg car (a box 1.8 x 0.5 x 4.2 m with its centre of mass 0.3 m low) at `position` with its rotation, and four wheels: the
		// front axle (0, z = -1.3: forward is -z) steers, the rear (1) is driven by the default differential.
		BodyKey make_test_car(SyncWorld& world, glm::vec3 position, const VehicleDesc& desc = test_car_desc(), glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f))
		{
			const BodyKey car = EntityGuid::generate();
			BodyDesc body = box_body(MotionType::Dynamic, position, glm::vec3(0.9f, 0.25f, 2.1f));
			body.Rotation = rotation;
			body.MassOverride = 1200.0f;
			body.CenterOfMassOffset = glm::vec3(0.0f, -0.3f, 0.0f);
			world.send(CreateBody{ car, body });
			world.send(CreateVehicle{ car, desc });
			return car;
		}

		const VehiclePose* find_vehicle(const PhysicsStepSnapshot& snapshot, BodyKey key)
		{
			for (const VehiclePose& vehicle : snapshot.Vehicles)
			{
				if (vehicle.Body == key)
				{
					return &vehicle;
				}
			}
			return nullptr;
		}

		// Steps one at a time with that input until `until` holds for the vehicle or `maxSteps` passed; returns the last snapshot.
		template<typename Until>
		PhysicsStepSnapshot drive(SyncWorld& world, BodyKey car, VehicleInput input, u32 maxSteps, Until until)
		{
			world.send(SetVehicleInput{ car, input });
			PhysicsStepSnapshot last;
			for (u32 i = 0; i < maxSteps; i++)
			{
				last = world.step();
				const VehiclePose* vehicle = find_vehicle(last, car);
				const BodyPose* body = find_pose(&last, car);
				if (vehicle != nullptr && body != nullptr && until(*vehicle, *body))
				{
					break;
				}
			}
			return last;
		}

		PhysicsStepSnapshot drive_for(SyncWorld& world, BodyKey car, VehicleInput input, u32 steps)
		{
			return drive(world, car, input, steps, [](const VehiclePose&, const BodyPose&) { return false; });
		}

		VehicleInput throttle(float amount, float steer = 0.0f) { return VehicleInput{ amount, 0.0f, steer, 0.0f }; }

		void check_vehicle_settles(Check& check)
		{
			SyncWorld world;
			check(world.start(kHz), "vehicle_settles: the world starts");
			make_vehicle_floor(world);
			const BodyKey car = make_test_car(world, glm::vec3(0.0f, 0.66f, 0.0f));
			const PhysicsStepSnapshot rested = world.step(2 * kHz);
			const VehiclePose* vehicle = find_vehicle(rested, car);
			const BodyPose* body = find_pose(&rested, car);
			check(vehicle != nullptr && vehicle->Wheels.size() == 4 && rested.VehicleCount == 1, "vehicle_made: the car is made with four wheels and counted");
			bool contact = vehicle != nullptr && !vehicle->Wheels.empty();
			float worst = 0.0f;
			for (const WheelPose& wheel : vehicle ? vehicle->Wheels : std::vector<WheelPose>{})
			{
				contact = contact && wheel.InContact;
				worst = std::max(worst, std::abs(wheel.SuspensionLength - wheel.RestLength));
			}
			check(contact, "vehicle_settles: every wheel touches the floor");
			check(contact && worst < 0.02f, "vehicle_settles: every suspension rests at its rest length, worst off by " + std::to_string(worst) + " m");
			check(body != nullptr && std::abs(body->Position.y - kRideHeight) < 0.02f, "vehicle_settles: the car rests with its wheels where they were authored, body at y " + std::to_string(body ? body->Position.y : 0.0f));
			check(body != nullptr && std::abs(body->LinearVelocity.y) < 0.01f, "vehicle_settles: it is at rest");
		}

		void check_vehicle_standstill_finite(Check& check)
		{
			SyncWorld world;
			check(world.start(kHz), "vehicle_standstill_finite: the world starts");
			make_vehicle_floor(world);
			const BodyKey car = make_test_car(world, glm::vec3(0.0f, 0.66f, 0.0f));
			world.step(kHz / 2);
			bool finite = true;
			const PhysicsStepSnapshot last = drive(world, car, throttle(1.0f), kHz, [&](const VehiclePose& vehicle, const BodyPose& body)
			{
				finite = finite && all_finite(vehicle) && finite_vec(body.Position) && finite_vec(body.LinearVelocity) && finite_vec(body.AngularVelocity);
				return false;
			});
			const VehiclePose* vehicle = find_vehicle(last, car);

			check(finite, "vehicle_standstill_finite: full throttle from rest gives finite values every step");
			check(vehicle != nullptr && vehicle->Speed < 15.0f, "vehicle_standstill_finite: no launch spike, " + std::to_string(vehicle ? vehicle->Speed : -1.0f) + " m/s after 1 s");
		}

		// Accelerates, then brakes to a stop; then a fresh car steers right at speed.
		void check_vehicle_drive_brake_steer(Check& check)
		{
			SyncWorld world;
			check(world.start(kHz), "vehicle_accelerates: the world starts");
			make_vehicle_floor(world);
			const BodyKey car = make_test_car(world, glm::vec3(0.0f, 0.66f, 0.0f));
			world.step(kHz / 2);

			bool drivenSpin = false;
			float loadSum = 0.0f;
			float grip = 0.0f;
			u32 sampled = 0;
			const PhysicsStepSnapshot fast = drive(world, car, throttle(1.0f), 8 * kHz, [&](const VehiclePose& vehicle, const BodyPose&)
			{

				if (++sampled == kHz && vehicle.Wheels.size() == 4)
				{
					drivenSpin = vehicle.Wheels[2].AngularVelocity > 0.0f && vehicle.Wheels[3].AngularVelocity > 0.0f;
					for (const WheelPose& wheel : vehicle.Wheels)
					{
						loadSum += wheel.Load;
					}
					grip = vehicle.Wheels[0].Grip;
				}
				return false;
			});
			const VehiclePose* vehicle = find_vehicle(fast, car);
			const BodyPose* body = find_pose(&fast, car);
			check(vehicle != nullptr && vehicle->Speed > 20.0f && vehicle->Speed < 70.0f, "vehicle_accelerates: 8 s of full throttle reach a road car's speed, got " + std::to_string(vehicle ? vehicle->Speed : -1.0f) + " m/s");
			check(vehicle != nullptr && vehicle->Gear >= 2, "vehicle_accelerates: the automatic gearbox shifted up, gear " + std::to_string(vehicle ? vehicle->Gear : -99));
			check(body != nullptr && body->Position.z < -10.0f, "vehicle_accelerates: it drove forward (-z), z " + std::to_string(body ? body->Position.z : 0.0f));
			check(drivenSpin, "vehicle_wheel_state: the driven wheels turn forward");
			check(std::abs(loadSum - 1200.0f * 9.81f) < 0.15f * 1200.0f * 9.81f, "vehicle_wheel_state: the wheel loads carry the car, sum " + std::to_string(loadSum) + " N");
			check(std::abs(grip - 1.0f) < 1e-4f, "vehicle_wheel_state: grip on a default floor is 1, got " + std::to_string(grip));

			bool stopped = false;
			drive(world, car, VehicleInput{ 0.0f, 1.0f, 0.0f, 0.0f }, 5 * kHz, [&](const VehiclePose& v, const BodyPose&) { stopped = std::abs(v.Speed) < 0.5f; return stopped; });
			check(stopped, "vehicle_brakes: full brake stops the car within 5 s");

			SyncWorld turning;
			turning.start(kHz);
			make_vehicle_floor(turning);
			const BodyKey turner = make_test_car(turning, glm::vec3(0.0f, 0.66f, 0.0f));
			turning.step(kHz / 2);
			drive(turning, turner, throttle(1.0f), 10 * kHz, [](const VehiclePose& v, const BodyPose&) { return v.Speed >= 15.0f; });
			const PhysicsStepSnapshot turned = drive_for(turning, turner, throttle(0.3f, 1.0f), kHz);
			const BodyPose* turnedBody = find_pose(&turned, turner);
			const VehiclePose* turnedCar = find_vehicle(turned, turner);
			check(turnedBody != nullptr && forward_of(*turnedBody).x > 0.1f, "vehicle_steers: steer +1 turns the car right (+x)");
			check(turnedCar != nullptr && turnedCar->Wheels.size() == 4 && std::abs(turnedCar->Wheels[0].SteerAngle) > 0.01f && turnedCar->Wheels[2].SteerAngle == 0.0f,
				"vehicle_steers: the front wheels steer, the rear do not");
		}

		// Lateral acceleration (yaw rate times speed) averaged over 1 s of a steady turn at about 15 m/s, on a floor with that grip.
		float turn_acceleration(float tireGrip)
		{
			SyncWorld world;
			world.start(kHz);
			PhysicsMaterialData material = default_physics_material();
			material.TireGrip = tireGrip;
			make_vehicle_floor(world, material);
			const BodyKey car = make_test_car(world, glm::vec3(0.0f, 0.66f, 0.0f));
			world.step(kHz / 2);
			drive(world, car, throttle(1.0f), 10 * kHz, [](const VehiclePose& v, const BodyPose&) { return v.Speed >= 15.0f; });
			drive_for(world, car, throttle(0.3f, 0.5f), kHz);
			float sum = 0.0f;
			u32 count = 0;
			drive(world, car, throttle(0.3f, 0.5f), kHz, [&](const VehiclePose& v, const BodyPose& b)
			{
				sum += std::abs(b.AngularVelocity.y * v.Speed);
				count++;
				return false;
			});
			return count > 0 ? sum / static_cast<float>(count) : 0.0f;
		}

		void check_vehicle_surface_grip(Check& check)
		{
			const float grippy = turn_acceleration(1.0f);
			const float slippery = turn_acceleration(0.4f);
			check(grippy > 1.0f && slippery < 0.75f * grippy, "vehicle_surface_grip: less TireGrip gives less cornering, " + std::to_string(grippy) + " vs " + std::to_string(slippery) + " m/s2");
		}

		void check_vehicle_lifetime(Check& check)
		{
			SyncWorld world;
			check(world.start(kHz), "vehicle_waits_for_body: the world starts");
			make_vehicle_floor(world);
			const BodyKey car = EntityGuid::generate();
			world.send(CreateVehicle{ car, test_car_desc() });
			check(world.step().VehicleCount == 0, "vehicle_waits_for_body: no vehicle without its body");
			BodyDesc body = box_body(MotionType::Dynamic, glm::vec3(0.0f, 0.66f, 0.0f), glm::vec3(0.9f, 0.25f, 2.1f));
			body.MassOverride = 1200.0f;
			world.send(CreateBody{ car, body });
			check(world.step().VehicleCount == 1, "vehicle_waits_for_body: made once the body exists");
			world.send(DestroyBody{ car });
			check(world.step().VehicleCount == 0, "vehicle_waits_for_body: unmade with its body");
			world.send(CreateBody{ car, body });
			check(world.step().VehicleCount == 1, "vehicle_waits_for_body: made again with a new body");
			world.send(DestroyVehicle{ car });
			check(world.step().VehicleCount == 0, "vehicle_destroy: DestroyVehicle removes it");

			// a rebuild (the settings changed) keeps the body's motion
			const BodyKey mover = make_test_car(world, glm::vec3(10.0f, 0.66f, 0.0f));
			world.step(kHz / 2);
			const PhysicsStepSnapshot moving = drive(world, mover, throttle(1.0f), 10 * kHz, [](const VehiclePose& v, const BodyPose&) { return v.Speed >= 15.0f; });
			const VehiclePose* before = find_vehicle(moving, mover);
			VehicleSettingsData stronger = default_vehicle_settings();
			stronger.MaxBrakeTorque = 3000.0f;
			world.send(CreateVehicle{ mover, test_car_desc(stronger) });
			const PhysicsStepSnapshot after = world.step();
			const VehiclePose* rebuilt = find_vehicle(after, mover);
			check(before != nullptr && rebuilt != nullptr && std::abs(rebuilt->Speed - before->Speed) < 0.05f * before->Speed, "vehicle_rebuild_keeps_motion: remaking the vehicle keeps its speed");
		}

		glm::vec3 scripted_run()
		{
			SyncWorld world;
			world.start(kHz);
			make_vehicle_floor(world);
			const BodyKey car = make_test_car(world, glm::vec3(0.0f, 0.66f, 0.0f));
			world.step(kHz / 2);
			drive_for(world, car, throttle(1.0f), 2 * kHz);
			const PhysicsStepSnapshot last = drive_for(world, car, throttle(0.5f, 0.7f), kHz);
			const BodyPose* body = find_pose(&last, car);
			return body ? body->Position : glm::vec3(1e9f);
		}

		void check_vehicle_repeatable(Check& check)
		{
			const glm::vec3 a = scripted_run();
			const glm::vec3 b = scripted_run();
			check(glm::length(a - b) < 1e-4f, "vehicle_repeatable: the same inputs give the same pose, " + fmt_vec(a) + " vs " + fmt_vec(b));
		}

		void check_vehicle_debug_draw(Check& check)
		{
			const auto lines = [](bool withCar)
			{
				SyncWorld world;
				world.start(kHz);
				make_vehicle_floor(world);
				if (withCar)
				{
					make_test_car(world, glm::vec3(0.0f, 0.66f, 0.0f));
				}
				else
				{
					BodyDesc body = box_body(MotionType::Dynamic, glm::vec3(0.0f, 0.66f, 0.0f), glm::vec3(0.9f, 0.25f, 2.1f));
					world.send(CreateBody{ EntityGuid::generate(), body });
				}
				world.send(SetDebugDraw{ true });
				const PhysicsStepSnapshot snapshot = world.step(2);
				return snapshot.Debug ? snapshot.Debug->Lines.size() : size_t(0);
			};
			const size_t without = lines(false);
			if (without > 0) // a build without Jolt's debug renderer (Shipping) draws nothing at all
			{
				const size_t with = lines(true);
				check(with > without, "vehicle_debug_draw: the vehicle adds lines to the debug drawing, " + std::to_string(with) + " vs " + std::to_string(without));
			}
		}

		void check_vehicle_holds_on_slope(Check& check)
		{
			SyncWorld world;
			check(world.start(kHz), "vehicle_holds_on_slope: the world starts");
			const float tilt = 20.0f;
			make_vehicle_floor(world, default_physics_material(), tilt);
			const glm::quat rotation = glm::angleAxis(glm::radians(tilt), glm::vec3(1.0f, 0.0f, 0.0f));
			const BodyKey car = make_test_car(world, rotation * glm::vec3(0.0f, 0.68f, 0.0f), test_car_desc(), rotation);
			const VehicleInput hold{ 0.0f, 1.0f, 0.0f, 1.0f };
			const PhysicsStepSnapshot settled = drive_for(world, car, hold, kHz);
			const PhysicsStepSnapshot later = drive_for(world, car, hold, 5 * kHz);
			const BodyPose* a = find_pose(&settled, car);
			const BodyPose* b = find_pose(&later, car);
			const float moved = a && b ? glm::length(b->Position - a->Position) : 1e9f;
			// Jolt's friction is a velocity constraint and drifts about 1 cm in 5 s; a cap that is the force at the current slip lets the car creep
			// several times that (measured 7 cm)
			check(moved < 0.03f, "vehicle_holds_on_slope: braked on a 20 degree slope it stays put (static grip), moved " + std::to_string(moved) + " m in 5 s");
		}

		void check_vehicle_no_chatter(Check& check)
		{
			SyncWorld world;
			check(world.start(kHz), "vehicle_no_chatter: the world starts");
			make_vehicle_floor(world);
			const BodyKey car = make_test_car(world, glm::vec3(0.0f, 0.66f, 0.0f));
			world.step(kHz / 2);
			drive(world, car, throttle(1.0f), 10 * kHz, [](const VehiclePose& v, const BodyPose&) { return v.Speed >= 15.0f; });
			drive_for(world, car, throttle(0.3f, 0.4f), 2 * kHz);
			float worst = 0.0f;
			float previous = 0.0f;
			bool first = true;
			drive(world, car, throttle(0.3f, 0.4f), kHz, [&](const VehiclePose& v, const BodyPose&)
			{
				if (v.Wheels.size() == 4)
				{
					if (!first)
					{
						worst = std::max(worst, std::abs(v.Wheels[0].SlipAngle - previous));
					}
					previous = v.Wheels[0].SlipAngle;
					first = false;
				}
				return false;
			});
			check(!first && worst < 0.05f, "vehicle_no_chatter: the front slip angle changes smoothly in a steady turn, worst step " + std::to_string(worst) + " rad");
		}
	}

	void run_vehicle_checks(Check& check, AssetPipelineModule& pipeline)
	{
		check_tire_model(check);
		check_physmat_tire_grip(check);
		check_body_com_offset(check);
		check_vehicle_asset(check, pipeline);
		check_vehicle_settles(check);
		check_vehicle_standstill_finite(check);
		check_vehicle_drive_brake_steer(check);
		check_vehicle_surface_grip(check);
		check_vehicle_lifetime(check);
		check_vehicle_repeatable(check);
		check_vehicle_debug_draw(check);
		check_vehicle_holds_on_slope(check);
		check_vehicle_no_chatter(check);
	}
}
