#include <editor_common.h>
#include "physics_smoke_test.h"

#include "../asset_pipeline/asset_database.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <cmath>
#include <string>
#include <thread>

#include <assets/asset_manager.h>
#include <core/engine.h>
#include <physics/physics_module.h>
#include <physics/physics_sync.h>
#include <scene/scene.h>

namespace wiwa
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		struct Check
		{
			u32 Failures = 0;

			void operator()(bool condition, const std::string& what)
			{
				if (!condition)
				{
					Failures++;
					WI_PHYSICS_LOG(Error, "Smoke test FAILED: {0}", what);
				}
			}
		};

		bool near_equal(double a, double b, double tolerance) { return std::abs(a - b) <= tolerance; }

		void check_module_registered(Check& check)
		{
			check(Engine::get().get_module<PhysicsModule>() != nullptr, "module_registered: the engine has a PhysicsModule");
		}

		// Started at 60 Hz and left alone for 500 ms, the thread has stepped about 30 times, and the published time only moves forward.
		void check_fixed_rate(Check& check)
		{
			PhysicsThread thread;
			PhysicsSettings settings;
			settings.FixedRateHz = 60;
			thread.start(settings);

			const Ref<const PhysicsStepSnapshot> first = thread.latest();
			check(first != nullptr, "fixed_rate_first_snapshot: start() publishes a step-0 snapshot");
			check(thread.is_running(), "fixed_rate_running: the thread runs after start()");

			const Clock::time_point begin = Clock::now();
			double lastTime = first ? first->Time : 0.0;
			u64 lastStep = first ? first->StepIndex : 0;
			bool monotonic = true;
			bool consistent = true;
			bool ordered = true;
			u32 samples = 0;
			while (Clock::now() - begin < std::chrono::milliseconds(500))
			{
				const Ref<const PhysicsStepSnapshot> previous = thread.previous();
				const Ref<const PhysicsStepSnapshot> latest = thread.latest();
				if (latest)
				{
					monotonic = monotonic && latest->Time >= lastTime && latest->StepIndex >= lastStep;
					consistent = consistent && near_equal(latest->Time, static_cast<double>(latest->StepIndex) / 60.0, 1e-6);
					lastTime = latest->Time;
					lastStep = latest->StepIndex;
					samples++;
				}
				if (previous && latest)
				{
					ordered = ordered && previous->StepIndex < latest->StepIndex && previous->Time < latest->Time;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			}

			const Ref<const PhysicsStepSnapshot> latest = thread.latest();
			const u64 steps = latest ? latest->StepIndex : 0;
			WI_PHYSICS_LOG(Info, "physics smoke: {0} steps in 500 ms at 60 Hz ({1} samples)", steps, samples);
			check(latest != nullptr && steps >= 25 && steps <= 35, "fixed_rate_60hz: 500 ms at 60 Hz gives 25..35 steps, got " + std::to_string(steps));
			check(latest != nullptr && latest->Time > (first ? first->Time : 0.0), "fixed_rate_time_advances: Time grows while the thread runs");
			check(monotonic, "fixed_rate_time_monotonic: Time and StepIndex never go back");
			check(consistent, "fixed_rate_time_matches_steps: Time is StepIndex fixed steps");
			check(latest != nullptr && near_equal(latest->FixedDt, 1.0 / 60.0, 1e-6), "fixed_rate_dt: FixedDt is 1/60");
			check(ordered, "fixed_rate_previous: previous() is older than latest()");

			// a SetSettings command takes effect on a later step
			PhysicsSettings faster;
			faster.FixedRateHz = 120;
			check(thread.push(SetSettings{ faster }), "set_settings_push: push succeeds while the thread runs");
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			const Ref<const PhysicsStepSnapshot> after = thread.latest();
			check(after != nullptr && near_equal(after->FixedDt, 1.0 / 120.0, 1e-6), "set_settings_applied: FixedDt is 1/120 after SetSettings(120 Hz)");
			check(after != nullptr && after->StepIndex > steps, "set_settings_keeps_stepping: the world keeps stepping after SetSettings");

			thread.stop();
			check(!thread.is_running(), "stop_not_running: is_running() is false after stop()");
			check(PhysicsThread::live_threads() == 0, "stop_no_thread_left: stop() leaves no physics thread running, " + std::to_string(PhysicsThread::live_threads()) + " left");
			check(thread.latest() == nullptr && thread.previous() == nullptr, "stop_frees_snapshots: stop() releases the snapshots");
			check(!thread.push(SetSettings{}), "stop_push_refused: push fails once the thread is stopped");
			thread.stop(); // a second stop does nothing
			check(PhysicsThread::live_threads() == 0, "stop_twice: a second stop() is harmless");
		}

		// Play, Stop, Play: the module builds a fresh world each time and stop leaves nothing behind.
		void check_start_stop_loop(Check& check)
		{
			PhysicsModule* module = Engine::get().get_module<PhysicsModule>();
			if (module == nullptr)
			{
				return;
			}

			check(!module->running(), "loop_idle: no world runs before play");
			for (u32 round = 0; round < 3; round++)
			{
				const std::string tag = " (round " + std::to_string(round) + ")";
				module->start_world(PhysicsSettings{});
				check(module->running(), "loop_running: start_world runs a world" + tag);
				check(PhysicsThread::live_threads() == 1, "loop_one_thread: one physics thread runs" + tag);
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
				const Ref<const PhysicsStepSnapshot> latest = module->thread().latest();
				check(latest != nullptr && latest->StepIndex >= 2, "loop_steps: the world steps" + tag);
				check(latest != nullptr && latest->StepIndex <= 12, "loop_fresh: each world starts again from step 0" + tag);

				// start_world while a world runs replaces it
				module->start_world(PhysicsSettings{});
				check(PhysicsThread::live_threads() == 1, "loop_restart_one_thread: start_world on a running world replaces it" + tag);
				const Ref<const PhysicsStepSnapshot> restarted = module->thread().latest();
				check(restarted != nullptr && restarted->StepIndex <= 1, "loop_restart_fresh: the replacement world starts from step 0" + tag);

				module->stop_world();
				check(!module->running(), "loop_stopped: stop_world stops the world" + tag);
				check(PhysicsThread::live_threads() == 0, "loop_no_thread_left: stop_world leaves no physics thread" + tag);
				check(module->thread().latest() == nullptr, "loop_snapshots_freed: stop_world releases the snapshots" + tag);
			}
			module->stop_world(); // stop without a world does nothing
			check(!module->running(), "loop_stop_idle: stop_world without a world is harmless");
		}

		// ---- bodies: an in-memory scene driven through PhysicsSync the way PhysicsModule drives the play scene ----

		BodyKey key_of(const Scene& scene, entt::entity entity) { return scene.registry().get<Id>(entity).Guid; }

		const BodyPose* find_pose(const PhysicsStepSnapshot* snapshot, BodyKey key)
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

		entt::entity add_entity(Scene& scene, const std::string& name, glm::vec3 position, entt::entity parent = entt::null)
		{
			const entt::entity entity = scene.create(name, parent);
			scene.registry().get<Transform>(entity).Position = position;
			return entity;
		}

		Collider& add_box_collider(Scene& scene, entt::entity entity, glm::vec3 halfExtents)
		{
			Collider collider;
			collider.Shape = ColliderShape::Box;
			collider.Size = halfExtents;
			return scene.registry().emplace_or_replace<Collider>(entity, collider);
		}

		RigidBody& add_body(Scene& scene, entt::entity entity, MotionType motion)
		{
			RigidBody body;
			body.Motion = motion;
			return scene.registry().emplace_or_replace<RigidBody>(entity, body);
		}

		// A ground: a static box whose top is at y = 0.5 (centre at 0, half height 0.5), no RigidBody.
		entt::entity add_ground(Scene& scene, glm::vec3 position = glm::vec3(0.0f))
		{
			const entt::entity ground = add_entity(scene, "Ground", position);
			add_box_collider(scene, ground, glm::vec3(20.0f, 0.5f, 20.0f));
			return ground;
		}

		// Starts a world for the scene and sends its bodies.
		bool start_scene(PhysicsThread& thread, PhysicsSync& sync, Scene& scene)
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
		Ref<const PhysicsStepSnapshot> run_for(PhysicsThread& thread, PhysicsSync& sync, Scene& scene, double seconds)
		{
			const Ref<const PhysicsStepSnapshot> first = thread.latest();
			const double until = (first ? first->Time : 0.0) + seconds;
			const Clock::time_point giveUp = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds * 3.0 + 2.0));
			Ref<const PhysicsStepSnapshot> latest = first;
			while (Clock::now() < giveUp)
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

		std::string fmt_vec(glm::vec3 v) { return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")"; }

		// Check 1: a dynamic box (half extent 0.5) dropped from 5 m onto a static box ground (top at 0.5) rests with its centre at 1.0.
		// Then: a kinematic body follows its entity, and destroying an entity removes its body.
		void check_drop_box(Check& check)
		{
			Scene scene;
			add_ground(scene);
			const entt::entity box = add_entity(scene, "Box", glm::vec3(0.0f, 5.0f, 0.0f));
			add_body(scene, box, MotionType::Dynamic);
			add_box_collider(scene, box, glm::vec3(0.5f));
			const entt::entity mover = add_entity(scene, "Mover", glm::vec3(50.0f, 10.0f, 0.0f));
			add_body(scene, mover, MotionType::Kinematic);
			add_box_collider(scene, mover, glm::vec3(0.5f));

			PhysicsThread thread;
			PhysicsSync sync;
			check(start_scene(thread, sync, scene), "drop_box_start: the world starts");
			check(sync.body_count() == 3, "drop_box_bodies: ground, box and mover are bodies, got " + std::to_string(sync.body_count()));

			const Ref<const PhysicsStepSnapshot> rested = run_for(thread, sync, scene, 4.0);
			const BodyPose* pose = find_pose(rested.get(), key_of(scene, box));
			const float y = pose != nullptr ? pose->Position.y : -1000.0f;
			WI_PHYSICS_LOG(Info, "physics smoke: dropped box rests at y = {0}", y);
			check(pose != nullptr && std::abs(y - 1.0f) <= 0.02f, "drop_box_rests: the dropped box rests at y = 1.0 +- 0.02, got " + std::to_string(y));
			check(pose != nullptr && std::abs(pose->Position.x) < 0.01f && std::abs(pose->Position.z) < 0.01f, "drop_box_straight: the box falls straight down");
			const float transformY = scene.registry().get<Transform>(box).Position.y;
			check(std::abs(transformY - 1.0f) <= 0.02f, "drop_box_transform: apply_poses writes the pose to the Transform, got y = " + std::to_string(transformY));

			// a kinematic body is driven from the scene
			scene.registry().get<Transform>(mover).Position = glm::vec3(52.0f, 10.0f, 0.0f);
			const Ref<const PhysicsStepSnapshot> moved = run_for(thread, sync, scene, 0.3);
			const BodyPose* moverPose = find_pose(moved.get(), key_of(scene, mover));
			check(moverPose != nullptr && glm::length(moverPose->Position - glm::vec3(52.0f, 10.0f, 0.0f)) < 0.01f,
				"kinematic_follows: a kinematic body moves to its entity's new pose, got " + (moverPose ? fmt_vec(moverPose->Position) : std::string("no pose")));
			check(moverPose != nullptr && glm::length(moverPose->LinearVelocity) < 0.001f, "kinematic_stops: the kinematic body stops at its target");
			check(std::abs(scene.registry().get<Transform>(mover).Position.x - 52.0f) < 1e-4f, "kinematic_not_written: a kinematic body's Transform is not overwritten");

			// destroying the entity removes the body
			const BodyKey boxKey = key_of(scene, box);
			scene.destroy(box);
			const Ref<const PhysicsStepSnapshot> after = run_for(thread, sync, scene, 0.1);
			check(!sync.has_body(boxKey) && after != nullptr && find_pose(after.get(), boxKey) == nullptr, "destroy_removes_body: a destroyed entity's body is gone");
			check(after != nullptr && after->Poses.size() == 2, "destroy_others_stay: the other bodies stay, got " + std::to_string(after ? after->Poses.size() : 0));
			thread.stop();
		}

		// Check 2: a stack of 5 boxes is asleep after 5 s.
		void check_stack_sleeps(Check& check)
		{
			Scene scene;
			add_ground(scene);
			std::vector<entt::entity> boxes;
			for (u32 i = 0; i < 5; i++)
			{
				const entt::entity box = add_entity(scene, "Stacked" + std::to_string(i), glm::vec3(0.0f, 1.0f + static_cast<float>(i) * 1.01f, 0.0f));
				add_body(scene, box, MotionType::Dynamic);
				add_box_collider(scene, box, glm::vec3(0.5f));
				boxes.push_back(box);
			}

			PhysicsThread thread;
			PhysicsSync sync;
			check(start_scene(thread, sync, scene), "stack_start: the world starts");
			const Ref<const PhysicsStepSnapshot> later = run_for(thread, sync, scene, 5.0);
			u32 asleep = 0;
			u32 found = 0;
			float top = 0.0f;
			for (const entt::entity box : boxes)
			{
				if (const BodyPose* pose = find_pose(later.get(), key_of(scene, box)))
				{
					found++;
					asleep += pose->Active ? 0 : 1;
					top = std::max(top, pose->Position.y);
				}
			}
			WI_PHYSICS_LOG(Info, "physics smoke: stack of 5, {0} found, {1} asleep, top at y = {2}", found, asleep, top);
			check(found == 5 && asleep == 5, "stack_sleeps: the 5 stacked boxes are asleep after 5 s, " + std::to_string(asleep) + " of " + std::to_string(found));
			check(found == 5 && std::abs(top - 5.0f) < 0.1f, "stack_stands: the stack still stands (top box at y = 5), got " + std::to_string(top));
			thread.stop();
		}

		// A Collider under a RigidBody ancestor (no RigidBody of its own) is part of the ancestor's compound shape; changing it replaces
		// the shape. Odd sizes and layers are clamped, never a Jolt assert.
		void check_compound(Check& check)
		{
			Scene scene;
			add_ground(scene);
			const entt::entity body = add_entity(scene, "Body", glm::vec3(0.0f, 3.0f, 0.0f));
			add_body(scene, body, MotionType::Dynamic);
			const entt::entity left = add_entity(scene, "Left", glm::vec3(-1.0f, 0.0f, 0.0f), body);
			add_box_collider(scene, left, glm::vec3(0.5f));
			const entt::entity right = add_entity(scene, "Right", glm::vec3(1.0f, 0.0f, 0.0f), body);
			add_box_collider(scene, right, glm::vec3(0.5f));

			// odd values: zero size, non-uniform scale on a sphere, a layer out of range, a mesh collider (no cooked shape yet)
			const entt::entity odd = add_entity(scene, "Odd", glm::vec3(30.0f, 5.0f, 0.0f));
			scene.registry().get<Transform>(odd).Scale = glm::vec3(1.0f, 3.0f, 0.0f);
			add_body(scene, odd, MotionType::Dynamic).MassOverride = -5.0f;
			Collider& oddCollider = add_box_collider(scene, odd, glm::vec3(0.0f));
			oddCollider.Shape = ColliderShape::Sphere;
			oddCollider.Layer = 99;
			const entt::entity flat = add_entity(scene, "Flat", glm::vec3(-30.0f, 5.0f, 0.0f));
			add_box_collider(scene, flat, glm::vec3(0.0f, -1.0f, 0.5f)).Layer = -3;
			const entt::entity meshy = add_entity(scene, "Meshy", glm::vec3(0.0f, 5.0f, 30.0f));
			add_box_collider(scene, meshy, glm::vec3(0.5f)).Shape = ColliderShape::Mesh;

			PhysicsThread thread;
			PhysicsSync sync;
			check(start_scene(thread, sync, scene), "compound_start: the world starts");
			check(sync.has_body(key_of(scene, body)) && !sync.has_body(key_of(scene, left)) && !sync.has_body(key_of(scene, right)),
				"compound_one_body: the colliders under a RigidBody make the ancestor's body, not their own");

			const Ref<const PhysicsStepSnapshot> rested = run_for(thread, sync, scene, 3.0);
			const BodyPose* pose = find_pose(rested.get(), key_of(scene, body));
			check(pose != nullptr && std::abs(pose->Position.y - 1.0f) <= 0.02f,
				"compound_rests: the compound of two child boxes rests at y = 1.0, got " + (pose ? std::to_string(pose->Position.y) : std::string("no pose")));
			check(find_pose(rested.get(), key_of(scene, left)) == nullptr, "compound_no_child_body: the child collider has no body of its own");
			check(find_pose(rested.get(), key_of(scene, odd)) != nullptr && find_pose(rested.get(), key_of(scene, flat)) != nullptr && find_pose(rested.get(), key_of(scene, meshy)) != nullptr,
				"odd_values_make_bodies: zero sizes, a squashed sphere, out-of-range layers and a mesh collider still make bodies");

			// bigger child boxes: ReplaceShape, and the body ends up higher
			scene.registry().get<Collider>(left).Size = glm::vec3(1.0f);
			scene.registry().get<Collider>(right).Size = glm::vec3(1.0f);
			const Ref<const PhysicsStepSnapshot> grown = run_for(thread, sync, scene, 2.0);
			const BodyPose* grownPose = find_pose(grown.get(), key_of(scene, body));
			check(grownPose != nullptr && std::abs(grownPose->Position.y - 1.5f) <= 0.03f,
				"compound_replace_shape: a changed child collider replaces the shape (rests at y = 1.5), got " + (grownPose ? std::to_string(grownPose->Position.y) : std::string("no pose")));
			thread.stop();
		}

		// Play, Stop, Play with two different scenes: the second world has only the second scene's bodies, its snapshots carry the new
		// generation, and commands meant for the first world are dropped.
		void check_replay_no_stale_bodies(Check& check)
		{
			Scene first;
			const entt::entity a = add_entity(first, "A", glm::vec3(0.0f, 2.0f, 0.0f));
			add_body(first, a, MotionType::Dynamic);
			add_box_collider(first, a, glm::vec3(0.5f));
			Scene second;
			const entt::entity b = add_entity(second, "B", glm::vec3(0.0f, 2.0f, 0.0f));
			add_body(second, b, MotionType::Dynamic);
			add_box_collider(second, b, glm::vec3(0.5f));

			PhysicsThread thread;
			PhysicsSync sync;
			check(start_scene(thread, sync, first), "replay_start_first: the first world starts");
			const u64 firstGeneration = sync.generation();
			run_for(thread, sync, first, 0.1);
			thread.stop();
			sync.clear();

			check(start_scene(thread, sync, second), "replay_start_second: the second world starts");
			check(!thread.push(CreateBody{ key_of(first, a), BodyDesc{} }, firstGeneration), "replay_stale_command_dropped: a command for the old world is refused");
			const Ref<const PhysicsStepSnapshot> latest = run_for(thread, sync, second, 0.2);
			check(latest != nullptr && latest->World == thread.generation() && latest->World != firstGeneration, "replay_new_generation: the snapshots carry the new world's generation");
			check(latest != nullptr && latest->Poses.size() == 1 && find_pose(latest.get(), key_of(second, b)) != nullptr && find_pose(latest.get(), key_of(first, a)) == nullptr,
				"replay_no_stale_bodies: the new world has only the new scene's bodies, got " + std::to_string(latest ? latest->Poses.size() : 0));

			// a snapshot of the old world does not move the new scene's entities
			PhysicsStepSnapshot stale;
			stale.World = firstGeneration;
			stale.Poses.push_back(BodyPose{ key_of(second, b), glm::vec3(0.0f, 100.0f, 0.0f) });
			const glm::vec3 before = second.registry().get<Transform>(b).Position;
			sync.apply_poses(second, stale, stale, 1.0f);
			check(second.registry().get<Transform>(b).Position == before, "replay_stale_snapshot_ignored: apply_poses ignores a snapshot of another world");
			thread.stop();
		}
	}

	namespace
	{
		// While paused (the game clock is paused) the world does not step, but it keeps taking the commands: pushing more than the queue
		// holds must not block for good (the main thread would never get to unpause). And a world started after a paused one runs.
		void check_paused_queue(Check& check)
		{
			Scene scene;
			const entt::entity wall = add_entity(scene, "Wall", glm::vec3(0.0f));
			add_box_collider(scene, wall, glm::vec3(0.5f));

			PhysicsThread thread;
			PhysicsSync sync;
			check(start_scene(thread, sync, scene), "paused_start: the world starts");
			thread.set_paused(true);
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			const Ref<const PhysicsStepSnapshot> before = thread.latest();

			// a pusher thread, so that a push that blocks for good fails the check instead of hanging the run
			const BodyKey key = key_of(scene, wall);
			const u32 count = PHYSICS_COMMAND_QUEUE_CAPACITY * 2 + 100;
			std::atomic<u32> pushed{ 0 };
			std::thread pusher([&]()
			{
				for (u32 i = 0; i < count; i++)
				{
					if (!thread.push(SetTransform{ key, glm::vec3(0.0f, static_cast<float>(i) * 0.001f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f) }, sync.generation()))
					{
						break;
					}
					pushed++;
				}
			});
			const Clock::time_point giveUp = Clock::now() + std::chrono::seconds(5);
			while (pushed < count && Clock::now() < giveUp)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
			const u32 done = pushed;
			const Ref<const PhysicsStepSnapshot> during = thread.latest();
			thread.stop(); // releases a push that is still blocked
			pusher.join();
			check(done == count, "paused_push_never_hangs: " + std::to_string(count) + " commands pushed while paused within 5 s, got " + std::to_string(done));
			check(before != nullptr && during != nullptr && during->StepIndex == before->StepIndex, "paused_no_steps: a paused world does not step");

			// Pause, Stop, Play: the new world is not paused
			thread.set_paused(true);
			check(start_scene(thread, sync, scene), "paused_restart: the world starts again");
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
			const Ref<const PhysicsStepSnapshot> restarted = thread.latest();
			check(restarted != nullptr && restarted->StepIndex >= 5, "paused_restart_runs: a world started after a paused one steps, got step " + std::to_string(restarted ? restarted->StepIndex : 0));
			thread.stop();
		}
	}

	namespace
	{
		// Peak height of the first bounce above the resting height, measured from the snapshot poses: the body falls, hits (its velocity
		// turns upwards) and rises to the top of the first bounce.
		struct BounceTracker
		{
			bool Bounced = false;  // the body has been moving up
			bool Peaked = false;   // and has turned down again
			float Peak = 0.0f;     // highest y since the first upward move

			void sample(const BodyPose& pose)
			{
				if (!Bounced && pose.LinearVelocity.y > 0.5f)
				{
					Bounced = true;
				}
				if (Bounced && !Peaked)
				{
					Peak = std::max(Peak, pose.Position.y);
					if (pose.LinearVelocity.y < -0.1f)
					{
						Peaked = true;
					}
				}
			}
		};

		bool write_text_file(const std::filesystem::path& path, const std::string& text)
		{
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			out << text;
			return static_cast<bool>(out);
		}

		// Check 5: physics materials. Four balls (radius 0.5) fall 4 m onto a ground with the default material (resting height 1.0):
		//  - Dead: a .wphysmat with restitution 0; Bouncy: restitution 0.8 combined with Max; both from the start;
		//  - Late: no material at first, Bouncy is assigned while it falls (the asset arrives after the body was made: SetMaterial);
		//  - Missing: a reference to an asset that does not exist (the default material is used, the body still works).
		// The first bounce of Bouncy is about 0.8^2 * 4 = 2.56 m high, Dead's none.
		void check_materials(Check& check, AssetPipelineModule& pipeline)
		{
			AssetDatabase& database = pipeline.get_database();
			const std::filesystem::path dir = database.get_assets_root() / "zz_physmat_phys_smoke";
			std::error_code error;
			std::filesystem::remove_all(dir, error);
			std::filesystem::create_directories(dir, error);
			check(write_text_file(dir / "dead.wphysmat", "{ \"friction\": 0.5, \"restitution\": 0.0 }") &&
				write_text_file(dir / "bouncy.wphysmat", "{ \"friction\": 0.5, \"restitution\": 0.8, \"restitutionCombine\": \"Max\" }"), "materials_files: the material files are written");
			const std::optional<AssetRef> dead = database.ensure_imported(dir / "dead.wphysmat");
			const std::optional<AssetRef> bouncy = database.ensure_imported(dir / "bouncy.wphysmat");
			check(dead.has_value() && bouncy.has_value(), "materials_import: both material files import");

			Scene scene;
			add_ground(scene);
			const auto ball = [&](const char* name, float x, AssetRef material)
			{
				const entt::entity entity = add_entity(scene, name, glm::vec3(x, 5.0f, 0.0f));
				add_body(scene, entity, MotionType::Dynamic);
				Collider& collider = add_box_collider(scene, entity, glm::vec3(0.5f));
				collider.Shape = ColliderShape::Sphere;
				collider.Material = material;
				return entity;
			};
			const entt::entity deadBall = ball("Dead", -6.0f, dead.value_or(AssetRef{}));
			const entt::entity bouncyBall = ball("Bouncy", -2.0f, bouncy.value_or(AssetRef{}));
			const entt::entity lateBall = ball("Late", 2.0f, AssetRef{});
			const entt::entity missingBall = ball("Missing", 6.0f, AssetRef{ AssetGuid::generate(), 0 });

			PhysicsThread thread;
			PhysicsSync sync;
			check(start_scene(thread, sync, scene), "materials_start: the world starts");

			BounceTracker deadTrack, bouncyTrack, lateTrack, missingTrack;
			const auto sample_all = [&](const Ref<const PhysicsStepSnapshot>& snapshot)
			{
				const std::pair<entt::entity, BounceTracker*> balls[] = { { deadBall, &deadTrack }, { bouncyBall, &bouncyTrack }, { lateBall, &lateTrack }, { missingBall, &missingTrack } };
				for (const auto& [entity, track] : balls)
				{
					if (const BodyPose* pose = find_pose(snapshot.get(), key_of(scene, entity)))
					{
						track->sample(*pose);
					}
				}
			};

			// the material arrives while the late ball falls (about 0.9 s from drop to the ground)
			run_for(thread, sync, scene, 0.3);
			if (bouncy)
			{
				scene.registry().get<Collider>(lateBall).Material = *bouncy;
			}
			for (u32 step = 0; step < 240 && !(deadTrack.Peaked && bouncyTrack.Peaked && lateTrack.Peaked); step++)
			{

				sample_all(run_for(thread, sync, scene, 1.0 / 120.0));
			}

			const auto height = [](const BounceTracker& track) { return track.Bounced ? track.Peak - 1.0f : 0.0f; }; // no bounce at all: 0
			const float deadHeight = height(deadTrack);
			const float bouncyHeight = height(bouncyTrack);
			const float lateHeight = height(lateTrack);
			WI_PHYSICS_LOG(Info, "physics smoke: first bounce heights dead {0}, bouncy {1}, late {2}, missing {3} (peaked: {4}, {5}, {6})", deadHeight, bouncyHeight, lateHeight,
				height(missingTrack), deadTrack.Peaked, bouncyTrack.Peaked, lateTrack.Peaked);
			check(bouncyTrack.Bounced && bouncyTrack.Peaked, "materials_bouncy_bounces: the ball with restitution 0.8 bounces");
			check(bouncyHeight - deadHeight > 0.5f, "materials_restitution_differs: the first bounce of restitution 0.8 is more than 0.5 m higher than restitution 0, got " +
				std::to_string(bouncyHeight) + " vs " + std::to_string(deadHeight));
			check(bouncyHeight > 1.5f && bouncyHeight < 3.2f, "materials_bounce_height: restitution 0.8 (Max) bounces about 2.5 m, got " + std::to_string(bouncyHeight));
			check(deadHeight < 0.2f, "materials_dead_stays: restitution 0 does not bounce, got " + std::to_string(deadHeight));
			check(lateTrack.Bounced && lateHeight > 1.5f, "materials_late_asset_applies: a material that arrives while the body falls applies (SetMaterial), got " +
				std::to_string(lateHeight));
			check(height(missingTrack) < 0.2f, "materials_missing_default: a reference to a missing asset uses the default material (no bounce)");

			// after a while every ball rests, the missing one included (it is a working body)
			const Ref<const PhysicsStepSnapshot> later = run_for(thread, sync, scene, 1.0);
			const BodyPose* missingPose = find_pose(later.get(), key_of(scene, missingBall));
			check(missingPose != nullptr && std::abs(missingPose->Position.y - 1.0f) < 0.05f && sync.has_body(key_of(scene, missingBall)),
				"materials_missing_works: the body of a missing material rests on the ground");
			thread.stop();

			std::filesystem::remove_all(dir, error);
			database.scan();
		}
	}

	bool run_physics_smoke_test(AssetPipelineModule& pipeline)
	{
		Check check;

		check_module_registered(check);
		check_fixed_rate(check);
		check_start_stop_loop(check);
		check_drop_box(check);
		check_stack_sleeps(check);
		check_compound(check);
		check_replay_no_stale_bodies(check);
		check_paused_queue(check);
		check_materials(check, pipeline);

		if (check.Failures == 0)
		{
			WI_PHYSICS_LOG(Info, "Physics smoke test passed");
		}
		return check.Failures == 0;
	}
}
