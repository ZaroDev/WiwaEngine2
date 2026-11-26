#pragma once

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <physics/jolt_runtime.h>
#include <physics/physics_thread.h>
#include <physics/physics_world.h>
#include <physics/physics_snapshot.h>
#include <physics/physics_commands.h>

#include <test_kit.h>

// Helpers shared by the physics smoke checks (physics_smoke_test.cpp, vehicle_checks.cpp).
namespace wiwa::physics_test
{
	struct CheckLog
	{
		static constexpr const char* Area = "Physics";
		static constexpr const char* Prefix = "Smoke test";
	};
	using Check = BasicCheck<CheckLog>;

	inline const BodyPose* find_pose(const PhysicsStepSnapshot* snapshot, BodyKey key)
	{
		if (snapshot == nullptr)
		{
			return nullptr;
		}
		for (const BodyPose& pose : snapshot->Poses)
		{
			if (pose.Key == key)
			{
				return &pose;
			}
		}
		return nullptr;
	}

	inline std::string fmt_vec(glm::vec3 v) { return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")"; }

	inline BodyDesc box_body(MotionType motion, glm::vec3 position, glm::vec3 halfExtents = glm::vec3(0.5f))
	{
		BodyDesc desc;
		desc.Motion = motion;
		desc.Position = position;
		desc.Shape.Kind = ShapeKind::Box;
		desc.Shape.Size = halfExtents;
		return desc;
	}

	// A world with commands pushed straight to it; every published step can be walked through with take_steps().
	struct CommandWorld
	{
		PhysicsThread Thread;
		u64 Generation = 0;

		bool start(glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f))
		{
			PhysicsSettings settings;
			settings.Gravity = gravity;
			if (!Thread.start(settings))
			{
				return false;
			}
			Generation = Thread.generation();
			return true;
		}

		void send(PhysicsCommand command) { Thread.push(std::move(command), Generation); }

		// Every step published until `steps` more have completed, oldest first.
		std::vector<Ref<const PhysicsStepSnapshot>> run_steps(u32 steps)
		{
			std::vector<Ref<const PhysicsStepSnapshot>> seen;
			const std::chrono::steady_clock::time_point giveUp = std::chrono::steady_clock::now() + std::chrono::seconds(20);
			while (seen.size() < steps && std::chrono::steady_clock::now() < giveUp)
			{
				for (Ref<const PhysicsStepSnapshot>& step : Thread.take_steps())
				{
					seen.push_back(std::move(step));
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			}
			return seen;
		}
	};

	inline BodyKey make_floor(CommandWorld& world)
	{
		const BodyKey floor = EntityGuid::generate();
		world.send(CreateBody{ floor, box_body(MotionType::Static, glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(40.0f, 0.5f, 10.0f)) });
		return floor;
	}

	// A world stepped on the calling thread, one fixed step at a time: commands land on a known step, so dynamics checks are repeatable
	// and do not wait on the wall clock. The threaded CommandWorld stays for lifetime and sync checks.
	struct SyncWorld
	{
		JoltRuntimeScope Runtime{};
		Scope<IPhysicsWorld> World{};

		bool start(u32 rateHz = 120, glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f))
		{
			PhysicsSettings settings;
			settings.Gravity = gravity;
			settings.FixedRateHz = rateHz;
			World = create_physics_world(settings, PhysicsLayers{});
			return World != nullptr;
		}

		void send(const PhysicsCommand& command) { World->execute(command); }

		// Runs `steps` steps and returns the snapshot of the last one.
		PhysicsStepSnapshot step(u32 steps = 1)
		{
			for (u32 i = 0; i < steps; i++)
			{
				World->step();
			}
			PhysicsStepSnapshot snapshot;
			snapshot.FixedDt = static_cast<float>(World->fixed_dt());
			World->write_poses(snapshot);
			World->take_events(snapshot);
			World->write_debug(snapshot);
			return snapshot;
		}
	};
}
