#include <pch.h>
#include "physics_module.h"
#include <algorithm>

#include <core/engine.h>
#include <assets/asset_manager.h>
#include <renderer/debug/debug_draw.h>
#include <renderer/render_module.h>
#include <scene/scene_module.h>

#include "physics_project_settings.h"

namespace wiwa
{
	void PhysicsModule::init()
	{
		// a game: the layers the editor packed with it (the editor has none in its Library and loads physics.wsettings itself)
		AssetManager* assets = Engine::get().get_module<AssetManager>();
		if (assets != nullptr && assets->get_source() != nullptr)
		{
			if (const std::optional<PhysicsLayers> layers = read_physics_layers(*assets->get_source()))
			{
				m_Layers = *layers;
			}
		}
	}

	void PhysicsModule::set_layers(const PhysicsLayers& layers)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Layers = layers;
		if (m_Thread.is_running())
		{
			m_Thread.push(SetLayers{ m_Layers }, m_Sync.generation());
		}
	}

	PhysicsModule::Stats PhysicsModule::stats() const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		Stats stats;
		stats.Running = m_Thread.is_running();
		stats.StepsLastFrame = static_cast<u32>(m_Steps.size());
		stats.DroppedSteps = m_Thread.folded_steps();
		if (m_Latest != nullptr)
		{
			stats.Bodies = m_Latest->BodyCount;
			stats.ActiveBodies = m_Latest->ActiveBodies;
			stats.Joints = m_Latest->JointCount;
			stats.Characters = m_Latest->CharacterCount;
			stats.StepMs = m_Latest->StepMs;
		}
		return stats;
	}

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
		m_Thread.push(SetLayers{ m_Layers });
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
		m_Thread.push(SetLayers{ m_Layers }, m_Sync.generation());
		if (m_DebugDraw)
		{
			m_Thread.push(SetDebugDraw{ true }, m_Sync.generation());
		}
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
		pump_locked();
		if (m_HasScene)
		{
			m_Thread.set_paused(!playing);
		}
	}

	void PhysicsModule::pump()
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		pump_locked();
	}

	void PhysicsModule::pump_locked()
	{
		// every step completed since the last frame, in order, with its events (a world that follows no scene: taken and dropped, so
		// they do not pile up)
		m_Steps.clear();
		if (m_Thread.is_running())
		{
			for (Ref<const PhysicsStepSnapshot>& step : m_Thread.take_steps())
			{
				if (m_HasScene && step->World == m_Sync.generation())
				{
					Engine::get().get_perf().add_physics_step(step->StepMs);
					m_Steps.push_back(std::move(step));
				}
			}
		}
		Engine::get().get_perf().set_physics_steps_last_frame(static_cast<u32>(m_Steps.size()));
		if (!m_HasScene)
		{
			return;
		}

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
			// an entity destroyed since the step: its events go (by guid, the entity may be gone before its DestroyBody was applied). A
			// joint that broke needs only the entity that owns it: it was connected to the world, or to an entity that may be gone.
			const bool needsOther = event.Kind != PhysicsEventKind::JointBreak;
			if (scene->find(event.A) != entt::null && (!needsOther || scene->find(event.B) != entt::null))
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

		submit_debug_draw_locked();
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

	void PhysicsModule::set_character_velocity(BodyKey key, glm::vec3 velocity) { submit(SetCharacterVelocity{ key, velocity }); }
	void PhysicsModule::character_jump(BodyKey key, float speed) { submit(CharacterJump{ key, speed }); }

	std::optional<CharacterPose> PhysicsModule::newest_character(BodyKey key) const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		if (m_Latest == nullptr || m_Latest->World != m_Sync.generation())
		{
			return std::nullopt;
		}
		for (const CharacterPose& pose : m_Latest->Characters)
		{
			if (pose.Key == key)
			{
				return pose;
			}
		}
		return std::nullopt;
	}

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

	void PhysicsModule::set_debug_draw(bool enabled)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_DebugDraw = enabled;
		if (m_Thread.is_running())
		{
			m_Thread.push(SetDebugDraw{ enabled }, m_Sync.generation());
		}
	}

	// The newest step's colliders go to the engine's debug renderer, for this frame.
	void PhysicsModule::submit_debug_draw_locked()
	{
		if (!m_DebugDraw || m_Latest == nullptr || m_Latest->Debug == nullptr || m_Latest->World != m_Sync.generation())
		{
			return;
		}
		RenderModule* renderer = Engine::get().get_module<RenderModule>();
		if (renderer == nullptr)
		{
			return;
		}

		DebugDraw& debug = renderer->get_debug_draw();
		const auto color = [](u32 packed)
		{
			return glm::vec4(static_cast<float>(packed & 255u), static_cast<float>((packed >> 8) & 255u), static_cast<float>((packed >> 16) & 255u), static_cast<float>(packed >> 24)) / 255.0f;
		};
		for (const PhysicsDebugLine& line : m_Latest->Debug->Lines)
		{
			debug.line(line.A, line.B, color(line.Color), DebugDepth::Tested, 1.0f, 0.0f);
		}
		for (const PhysicsDebugTriangle& triangle : m_Latest->Debug->Triangles)
		{
			debug.triangle(triangle.A, triangle.B, triangle.C, color(triangle.Color), DebugDepth::Tested, 0.0f);
		}
	}
}
