#include <pch.h>
#include "physics_module.h"

#include <algorithm>

#include <core/engine.h>
#include <scene/scene_module.h>

namespace wiwa
{
	void PhysicsModule::shutdown()
	{
		stop_world();
	}

	bool PhysicsModule::start_world(Ref<Scene> scene)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return start_locked(scene);
	}

	bool PhysicsModule::start_world(const PhysicsSettings& settings)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		stop_locked();
		if (!m_Thread.start(settings))
		{
			WI_PHYSICS_LOG(Error, "The physics world could not start");
			return false;
		}
		return true;
	}

	void PhysicsModule::stop_world()
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		stop_locked();
	}

	Ref<Scene> PhysicsModule::world_scene() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return m_Scene.lock();
	}

	bool PhysicsModule::start_locked(const Ref<Scene>& scene)
	{
		stop_locked();
		if (scene == nullptr)
		{
			return false;
		}

		scene->update_world_transforms();
		if (!m_Thread.start(scene->settings().Physics))
		{
			WI_PHYSICS_LOG(Error, "The physics world could not start: the scene plays without physics");
			m_FailedScene = scene;
			return false;
		}
		m_Thread.set_paused(!Engine::get().get_time().get_game_clock().IsPlaying); // the world follows the game clock from its first step
		m_Sync.build(*scene, m_Thread);
		m_Scene = scene;
		m_HasScene = true;
		m_SettingsRevision = scene->settings_revision();
		WI_PHYSICS_LOG(Info, "Physics world {0} started: {1} bodies", m_Thread.generation(), m_Sync.body_count());
		return true;
	}

	void PhysicsModule::stop_locked()
	{
		if (m_HasScene)
		{
			WI_PHYSICS_LOG(Info, "Physics world {0} stopped", m_Thread.generation());
		}
		m_Thread.stop();
		m_Sync.clear();
		m_Scene.reset();
		m_HasScene = false;
		m_Latest.reset();
		m_Previous.reset();
		m_Steps.clear();
	}

	void PhysicsModule::update(float)
	{
		SceneModule* scenes = Engine::get().get_module<SceneModule>();
		const Ref<Scene> active = scenes != nullptr ? scenes->get_active() : nullptr;
		const bool playing = Engine::get().get_time().get_game_clock().IsPlaying;

		std::lock_guard<std::mutex> lock(m_Mutex);
		if (m_HasScene && m_Scene.lock() != active)
		{
			stop_locked(); // the scene was replaced: its world goes with it
		}
		if (m_AutoStart && !m_HasScene && playing && active != nullptr && m_FailedScene.lock() != active)
		{
			start_locked(active);
		}
		// every step completed since the last frame, in order, with its events (a world that follows no scene: taken and dropped, so
		// they do not pile up)
		m_Steps.clear();
		if (m_Thread.is_running())
		{
			for (Ref<const PhysicsStepSnapshot>& step : m_Thread.take_steps())
			{
				if (m_HasScene && step->World == m_Sync.generation())
				{
					m_Steps.push_back(std::move(step));
				}
			}
		}
		if (!m_HasScene)
		{
			return;
		}

		m_Thread.set_paused(!playing);

		// the newest step, and the one just before it for interpolation
		const Ref<const PhysicsStepSnapshot> latest = m_Thread.latest();
		if (latest != nullptr && latest->World == m_Sync.generation() && (m_Latest == nullptr || latest->StepIndex != m_Latest->StepIndex))
		{
			const Ref<const PhysicsStepSnapshot> previous = m_Thread.previous();
			m_Previous = previous != nullptr && previous->World == latest->World && previous->StepIndex + 1 == latest->StepIndex ? previous : m_Latest;
			m_Latest = latest;
			m_LatestArrival = std::chrono::steady_clock::now();
		}
	}

	std::vector<Ref<const PhysicsStepSnapshot>> PhysicsModule::steps_this_frame() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return m_Steps;
	}

	PhysicsDispatchResult PhysicsModule::dispatch_steps(IPhysicsStepSink& sink)
	{
		const std::vector<Ref<const PhysicsStepSnapshot>> steps = steps_this_frame();
		return dispatch_physics_steps(steps, [this](const PhysicsStepSnapshot& step) { return events_for_step(step); }, sink, m_DispatchQuery);
	}

	Ref<const QuerySnapshot> PhysicsModule::query() const
	{
		if (m_DispatchQuery != nullptr)
		{
			return m_DispatchQuery;
		}
		std::lock_guard<std::mutex> lock(m_Mutex);
		return m_Latest != nullptr ? m_Latest->Query : nullptr;
	}

	std::vector<PhysicsEvent> PhysicsModule::events_for_step(const PhysicsStepSnapshot& step) const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return events_for_step_locked(step);
	}

	std::vector<PhysicsEvent> PhysicsModule::events_for_step_locked(const PhysicsStepSnapshot& step) const
	{
		std::vector<PhysicsEvent> events;
		const Ref<Scene> scene = m_HasScene ? m_Scene.lock() : nullptr;
		if (scene == nullptr || step.World == 0 || step.World != m_Sync.generation())
		{
			return events; // another world's step: its bodies are not this scene's
		}
		events.reserve(step.Events.size());
		for (const PhysicsEvent& event : step.Events)
		{
			// an entity destroyed since the step: its events go (by guid, the entity may be gone before its DestroyBody was applied)
			if (scene->find(event.A) != entt::null && scene->find(event.B) != entt::null)
			{
				events.push_back(event);
			}
		}
		return events;
	}

	void PhysicsModule::pre_render_update(float)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		const Ref<Scene> scene = m_HasScene ? m_Scene.lock() : nullptr;
		if (scene == nullptr)
		{
			return;
		}

		if (scene->settings_revision() != m_SettingsRevision)
		{
			m_SettingsRevision = scene->settings_revision();
			m_Thread.push(SetSettings{ scene->settings().Physics }, m_Sync.generation());
		}

		// what the scripts and the editor moved this frame reaches the world, then the world's poses come back
		scene->update_world_transforms();
		m_Sync.push_changes(*scene, m_Thread);

		if (m_Latest != nullptr)
		{
			const float elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - m_LatestArrival).count();
			const float alpha = m_Latest->FixedDt > 0.0f ? std::clamp(elapsed / m_Latest->FixedDt, 0.0f, 1.0f) : 1.0f;
			const PhysicsStepSnapshot& previous = m_Previous != nullptr ? *m_Previous : *m_Latest;
			m_Sync.apply_poses(*scene, previous, *m_Latest, alpha);
			scene->update_world_transforms();
		}
	}

	void PhysicsModule::submit(PhysicsCommand&& command)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		if (m_Thread.is_running())
		{
			m_Thread.push(std::move(command), m_Sync.generation());
		}
	}

	void PhysicsModule::add_force(BodyKey key, glm::vec3 force) { submit(AddForce{ key, force }); }
	void PhysicsModule::add_impulse(BodyKey key, glm::vec3 impulse) { submit(AddImpulse{ key, impulse }); }
	void PhysicsModule::add_torque(BodyKey key, glm::vec3 torque) { submit(AddTorque{ key, torque }); }
	void PhysicsModule::set_velocity(BodyKey key, glm::vec3 velocity) { submit(SetVelocity{ key, velocity }); }
	void PhysicsModule::set_angular_velocity(BodyKey key, glm::vec3 angular) { submit(SetAngularVelocity{ key, angular }); }
	void PhysicsModule::wake(BodyKey key) { submit(WakeBody{ key }); }
	void PhysicsModule::set_gravity(glm::vec3 gravity) { submit(SetGravity{ gravity }); }
	void PhysicsModule::set_kinematic_target(BodyKey key, glm::vec3 position, glm::quat rotation) { submit(SetKinematicTarget{ key, position, rotation }); }

	std::optional<BodyPose> PhysicsModule::newest_pose(BodyKey key) const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		if (m_Latest == nullptr || m_Latest->World != m_Sync.generation())
		{
			return std::nullopt;
		}
		for (const BodyPose& pose : m_Latest->Poses)
		{
			if (pose.Key == key)
			{
				return pose;
			}
		}
		return std::nullopt;
	}

	std::optional<glm::vec3> PhysicsModule::velocity(BodyKey key) const
	{
		const std::optional<BodyPose> pose = newest_pose(key);
		return pose ? std::optional<glm::vec3>(pose->LinearVelocity) : std::nullopt;
	}

	std::optional<glm::vec3> PhysicsModule::angular_velocity(BodyKey key) const
	{
		const std::optional<BodyPose> pose = newest_pose(key);
		return pose ? std::optional<glm::vec3>(pose->AngularVelocity) : std::nullopt;
	}
}
