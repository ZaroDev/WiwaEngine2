#pragma once

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include <physics/jolt_runtime.h>
#include <physics/physics_thread.h>
#include <physics/physics_world.h>
#include <physics/physics_sync.h>
#include <scene/scene.h>
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

	// ---- scenes driven through PhysicsSync the way PhysicsModule drives the play scene ----

	inline BodyKey key_of(const Scene& scene, entt::entity entity) { return scene.registry().get<Id>(entity).Guid; }


	inline entt::entity add_entity(Scene& scene, const std::string& name, glm::vec3 position, entt::entity parent = entt::null)
	{
		const entt::entity entity = scene.create(name, parent);
		scene.registry().get<Transform>(entity).Position = position;
		return entity;
	}

	inline Collider& add_box_collider(Scene& scene, entt::entity entity, glm::vec3 halfExtents)
	{
		Collider collider;
		collider.Shape = ColliderShape::Box;
		collider.Size = halfExtents;
		return scene.registry().emplace_or_replace<Collider>(entity, collider);
	}

	inline RigidBody& add_body(Scene& scene, entt::entity entity, MotionType motion)
	{
		RigidBody body;
		body.Motion = motion;
		return scene.registry().emplace_or_replace<RigidBody>(entity, body);
	}

	// A ground: a static box whose top is at y = 0.5 (centre at 0, half height 0.5), no RigidBody.
	inline entt::entity add_ground(Scene& scene, glm::vec3 position = glm::vec3(0.0f))
	{
		const entt::entity ground = add_entity(scene, "Ground", position);
		add_box_collider(scene, ground, glm::vec3(20.0f, 0.5f, 20.0f));
		return ground;
	}

	// Starts a world for the scene and sends its bodies.
	inline bool start_scene(PhysicsThread& thread, PhysicsSync& sync, Scene& scene)
	{
		scene.update_world_transforms();
		if (!thread.start(scene.settings().Physics))
		{
			return false;
		}
		sync.build(scene, thread);
		return true;
	}

	// What PhysicsModule does every frame, for simulated seconds of physics time (real time: the thread runs on its own clock).
	inline Ref<const PhysicsStepSnapshot> run_for(PhysicsThread& thread, PhysicsSync& sync, Scene& scene, double seconds)
	{
		const Ref<const PhysicsStepSnapshot> first = thread.latest();
		const double until = (first ? first->Time : 0.0) + seconds;
		const std::chrono::steady_clock::time_point giveUp = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds * 3.0 + 2.0));
		Ref<const PhysicsStepSnapshot> latest = first;
		while (std::chrono::steady_clock::now() < giveUp)
		{
			scene.update_world_transforms();
			sync.push_changes(scene, thread);
			latest = thread.latest();
			const Ref<const PhysicsStepSnapshot> previous = thread.previous();
			if (latest != nullptr)
			{
				sync.apply_poses(scene, previous ? *previous : *latest, *latest, 1.0f);
				scene.update_world_transforms();
				if (latest->Time >= until)
				{
					break;
				}
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(8));
		}
		return latest;
	}
}
