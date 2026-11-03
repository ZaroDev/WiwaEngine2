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
		if (!m_HasScene)
		{
			return;
		}

		m_Thread.set_paused(!playing);

		// the newest step, and the one just before it for interpolation (events and per-step dispatch come with the contact listener)
		const Ref<const PhysicsStepSnapshot> latest = m_Thread.latest();
		if (latest != nullptr && latest->World == m_Sync.generation() && (m_Latest == nullptr || latest->StepIndex != m_Latest->StepIndex))
		{
			const Ref<const PhysicsStepSnapshot> previous = m_Thread.previous();
			m_Previous = previous != nullptr && previous->World == latest->World && previous->StepIndex + 1 == latest->StepIndex ? previous : m_Latest;
			m_Latest = latest;
			m_LatestArrival = std::chrono::steady_clock::now();
		}
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
}
