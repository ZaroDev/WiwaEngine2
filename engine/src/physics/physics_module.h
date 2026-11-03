#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <core/module.h>

#include "physics_dispatch.h"

#include "physics_sync.h"
#include "physics_thread.h"

namespace wiwa
{
	class Scene;

	// Owns the physics thread and its command and snapshot channels. A world exists only while a scene plays:
	//  - with auto start (the default, a game): a world is built for the SceneModule's active scene once the game clock plays, and built
	//    again when a load replaces that scene;
	//  - without (the editor): start_world(scene) and stop_world() are called by play mode, on the Play-mode clone.
	// Either way the world stops when its scene is no longer the active one, and the physics steps stop while the game clock is paused.
	// Per frame: update() takes the newest step snapshots; pre_render_update() sends the scene's changes (new, removed and changed
	// colliders, static and kinematic bodies that moved) and writes the interpolated poses of dynamic bodies into their Transforms.
	// Used by the game too, so nothing here depends on editor code. The physics thread never calls Engine::get_module.
	class PhysicsModule : public IModule
	{
	public:
		PhysicsModule() = default;
		virtual ~PhysicsModule() override = default;

		virtual void init() override;
		virtual void shutdown() override;
		virtual void update(float deltaTime) override;
		virtual void pre_render_update(float deltaTime) override;

		virtual UpdateFlags get_update_flags() const override { return UpdateFlags::UPDATE | UpdateFlags::UPDATE_RENDERER; }
		virtual std::string get_name() const override { return "PhysicsModule"; }
		virtual int get_priority() const override { return -20; } // after the scene (-80), before the UI (-10) and the scripts (0)

		// With auto start a world follows the active scene while the game clock plays (a game). The editor turns it off.
		void set_auto_start(bool autoStart) { m_AutoStart = autoStart; }

		PhysicsThread& thread() { return m_Thread; }
		bool running() const { return m_Thread.is_running(); }

		// Builds a new world for the scene (its physics settings, a body for each of its colliders) and keeps it in step with the scene
		// until stop_world, or until the scene is no longer the SceneModule's active one. A running world is stopped first. False (logged)
		// when the world could not be made: the scene then plays without physics.
		bool start_world(Ref<Scene> scene);
		// Builds a new, empty world that follows no scene (a running one is stopped first). It follows no game clock either: it is not paused.
		bool start_world(const PhysicsSettings& settings);
		// Stops the thread and frees the world. Does nothing when no world runs.
		void stop_world();

		// What update() does with the steps, without following the SceneModule or the game clock: takes the steps the world completed since the last
		// call (for dispatch_steps) and the newest one (for query and the poses). For a world started with start_world and driven by its owner.
		void pump();

		// The scene the world was built for; null when none.
		Ref<Scene> world_scene() const;

		// The steps the world completed since the previous frame (taken by update()), oldest first, for the per-step dispatch.
		std::vector<Ref<const PhysicsStepSnapshot>> steps_this_frame() const;

		// The step's contact and trigger events for the dispatcher, in the step's order: none when the step is another world's (an
		// older world, or none runs), and none naming an entity that is no longer in the world's scene (destroyed since).
		std::vector<PhysicsEvent> events_for_step(const PhysicsStepSnapshot& step) const;

		// Delivers the steps taken this frame to the sink (see dispatch_physics_steps). Main thread, after update().
		PhysicsDispatchResult dispatch_steps(IPhysicsStepSink& sink);

		// Body control for scripts (main thread). Each is a command for the world that runs, applied at the start of the next step (so the
		// effect shows in the snapshot of the step after the one that was current when it was sent). With no world running, or for a body that does
		// not exist (any more), nothing happens and nothing is logged.
		void add_force(BodyKey key, glm::vec3 force);
		void add_impulse(BodyKey key, glm::vec3 impulse);
		void add_torque(BodyKey key, glm::vec3 torque);
		void set_velocity(BodyKey key, glm::vec3 velocity);
		void set_angular_velocity(BodyKey key, glm::vec3 angular);
		void wake(BodyKey key);
		void set_gravity(glm::vec3 gravity);
		void set_kinematic_target(BodyKey key, glm::vec3 position, glm::quat rotation);

		// The project's physics layers (names and which pairs collide). A world starts with them and a running one takes the change at once.
		// A game reads them from its pack when the module starts; the editor sets them from physics.wsettings.
		const PhysicsLayers& layers() const { return m_Layers; }
		void set_layers(const PhysicsLayers& layers);

		struct Stats
		{
			u32 Bodies = 0;
			u32 ActiveBodies = 0;
			float StepMs = 0.0f;       // of the newest step
			u32 StepsLastFrame = 0;    // steps update() took in its last call
			u64 DroppedSteps = 0;      // folded into a later one because the main thread did not take them (see PhysicsThread::folded_steps)
			bool Running = false;
		};
		Stats stats() const;

		// The velocities of a body in the newest step; none when there is no such body in it.
		std::optional<glm::vec3> velocity(BodyKey key) const;
		std::optional<glm::vec3> angular_velocity(BodyKey key) const;

		// The data the queries run against: the step that is being delivered to the sink, else the newest step. Null while no world runs.
		Ref<const QuerySnapshot> query() const;

	private:
		std::vector<PhysicsEvent> events_for_step_locked(const PhysicsStepSnapshot& step) const;

		void submit(PhysicsCommand&& command);
		std::optional<BodyPose> newest_pose(BodyKey key) const;

		void pump_locked();
		bool start_locked(const Ref<Scene>& scene);
		void stop_locked();

		mutable std::mutex m_Mutex{}; // start_world and stop_world may come from another thread than the frame (the smoke tests)
		PhysicsThread m_Thread{};
		PhysicsSync m_Sync{};
		std::weak_ptr<Scene> m_Scene{};
		bool m_HasScene = false;
		std::weak_ptr<Scene> m_FailedScene{}; // auto start does not try the same scene again
		bool m_AutoStart = true;
		PhysicsLayers m_Layers{};
		u64 m_SettingsRevision = 0;

		std::vector<Ref<const PhysicsStepSnapshot>> m_Steps{}; // the steps completed since the previous frame
		Ref<const QuerySnapshot> m_DispatchQuery{}; // set while dispatch_steps delivers a step (main thread only)
		Ref<const PhysicsStepSnapshot> m_Latest{};
		Ref<const PhysicsStepSnapshot> m_Previous{};
		std::chrono::steady_clock::time_point m_LatestArrival{};
	};
}
