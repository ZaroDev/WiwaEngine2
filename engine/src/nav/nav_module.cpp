#include <pch.h>
#include "nav_module.h"

#include <algorithm>

#include <core/engine.h>
#include <scene/components.h>
#include <scene/scene.h>
#include <scene/scene_module.h>

#include "nav_project_settings.h"

namespace wiwa
{
	void NavModule::init()
	{
		// A game: the navigation settings the editor packed with it (the editor has none in its Library and applies nav.wsettings itself).
		AssetManager* assets = Engine::get().get_module<AssetManager>();
		if (assets && assets->get_source())
		{
			if (const std::optional<NavSettings> settings = read_nav_settings(*assets->get_source()))
			{
				set_settings(*settings);
			}
		}
	}

	void NavModule::shutdown()
	{
		stop_world();
	}

	void NavModule::update(float)
	{
		SceneModule* scenes = Engine::get().get_module<SceneModule>();
		Time& time = Engine::get().get_time();
		tick(scenes != nullptr ? scenes->get_active() : nullptr, time.get_game_clock().IsPlaying, time.get_delta_time_seconds());
	}

	void NavModule::warn_once(const char* message)
	{
		if (!m_Warned)
		{
			m_Warned = true;
			WI_NAV_LOG(Warning, "Navigation: {0}", message);
		}
	}

	void NavModule::tick(const Ref<Scene>& active, bool playing, float dt)
	{
		if (m_HasWorld)
		{
			const Ref<Scene> scene = m_World.lock();
			if (!scene || (m_AutoStart && scene != active))
			{
				stop_world(); // the scene was replaced or is gone: its agents go with it
			}
		}
		if (m_AutoStart && !m_HasWorld && playing && active != nullptr)
		{
			start_world(active);
		}
		if (!m_HasWorld)
		{
			return;
		}

		poll_loading();
		if (playing && m_Agents)
		{
			if (const Ref<Scene> scene = m_World.lock())
			{
				m_Agents->update(*scene, std::clamp(dt, 0.0f, 0.1f));
			}
		}
	}

	bool NavModule::start_world(Ref<Scene> scene)
	{
		stop_world();
		if (!scene)
		{
			return false;
		}
		m_World = scene;
		m_HasWorld = true;
		m_Warned = false;
		m_IgnoredVolumes = 0;

		// The first enabled volume with a baked mesh is the scene's navmesh.
		entt::registry& registry = scene->registry();
		AssetRef chosen{};
		entt::entity chosenEntity = entt::null;
		u32 enabled = 0;
		std::vector<entt::entity> volumes;
		for (auto [entity, volume] : registry.view<NavMeshVolume>().each())
		{
			if (volume.Enabled)
			{
				volumes.push_back(entity);
			}
		}
		std::sort(volumes.begin(), volumes.end(), [](entt::entity a, entt::entity b) { return entt::to_integral(a) < entt::to_integral(b); }); // the order they were made in
		enabled = static_cast<u32>(volumes.size());
		for (const entt::entity entity : volumes)
		{
			const NavMeshVolume& volume = registry.get<NavMeshVolume>(entity);
			if (chosenEntity == entt::null && volume.NavMesh.valid())
			{
				chosen = volume.NavMesh;
				chosenEntity = entity;
			}
			else if (chosenEntity != entt::null)
			{
				m_IgnoredVolumes++;
				const Name* name = registry.try_get<Name>(entity);
				WI_NAV_LOG(Warning, "Navigation: a second enabled NavMeshVolume ({0}) is ignored: the scene has one navmesh", name != nullptr ? name->Value : std::string("unnamed"));
			}
		}
		if (chosenEntity == entt::null)
		{
			warn_once(enabled == 0 ? "the scene has no enabled NavMeshVolume, so there is no navmesh" : "no enabled NavMeshVolume has a baked navmesh: bake it in the Navigation panel");
			return true;
		}

		AssetManager* assets = Engine::get().get_module<AssetManager>();
		if (assets == nullptr)
		{
			warn_once("there is no asset manager to load the navmesh with");
			return true;
		}
		m_Handle = assets->load<NavMeshData>(chosen);
		m_Loading = true;
		return true;
	}

	bool NavModule::start_world_with_mesh(Ref<Scene> scene, const NavMeshData& mesh)
	{
		stop_world();
		if (!scene)
		{
			return false;
		}
		m_World = scene;
		m_HasWorld = true;
		m_Warned = false;
		make_data(mesh);
		return true;
	}

	void NavModule::make_data(const NavMeshData& mesh)
	{
		std::string error;
		m_Data = NavData::create(mesh, error);
		if (!m_Data)
		{
			WI_NAV_LOG(Error, "Navigation: the navmesh cannot be used: {0}", error);
			return;
		}
		m_Query = CreateScope<NavQuery>(*m_Data);
		m_Agents = CreateScope<NavAgents>(*m_Data, m_Settings);
		m_Agents->OnState = [this](entt::entity entity, NavAgentState state)
		{
			const auto listeners = m_Listeners; // a listener may remove itself
			for (const auto& [id, listener] : listeners)
			{
				listener(entity, state);
			}
		};

		// Destinations asked for while it loaded.
		if (const Ref<Scene> scene = m_World.lock())
		{
			const auto queued = std::move(m_Queued);
			m_Queued.clear();
			for (const auto& [entity, destination] : queued)
			{
				if (!m_Agents->set_destination(*scene, entity, destination) && m_Agents->OnState)
				{
					m_Agents->OnState(entity, NavAgentState::NoPath); // asked for while loading: the script waits for an answer
				}
			}
		}
	}

	void NavModule::poll_loading()
	{
		if (!m_Loading)
		{
			return;
		}
		switch (m_Handle.state())
		{
		case LoadState::Ready:
			m_Loading = false;
			make_data(*m_Handle.get());
			break;
		case LoadState::Failed:
			m_Loading = false;
			WI_NAV_LOG(Error, "Navigation: the navmesh asset failed to load; rebake it in the Navigation panel");
			for (const auto& [entity, destination] : m_Queued)
			{
				const auto listeners = m_Listeners;
				for (const auto& [id, listener] : listeners)
				{
					listener(entity, NavAgentState::NoPath);
				}
			}
			m_Queued.clear();
			break;
		default:
			break;
		}
	}

	void NavModule::stop_world()
	{
		if (!m_HasWorld)
		{
			return;
		}
		m_Agents.reset(); // before the data they stand on
		m_Query.reset();
		m_Data.reset();
		m_Handle = {};
		m_Loading = false;
		m_Queued.clear();
		m_World.reset();
		m_HasWorld = false;
	}

	NavFilter NavModule::filter_for(u32 areaMask) const
	{
		NavFilter filter;
		filter.AreaMask = areaMask;
		filter.AreaCost = m_Settings.AreaCost;
		return filter;
	}

	bool NavModule::set_destination(entt::entity entity, glm::vec3 destination)
	{
		const Ref<Scene> scene = world_scene();
		if (!scene)
		{
			return false;
		}
		if (m_Loading)
		{
			m_Queued[entity] = destination;
			return true;
		}
		if (!m_Agents)
		{
			warn_once("a destination was set but the scene has no navmesh");
			return false;
		}
		return m_Agents->set_destination(*scene, entity, destination);
	}

	void NavModule::stop(entt::entity entity)
	{
		m_Queued.erase(entity);
		if (m_Agents)
		{
			m_Agents->stop(entity);
		}
	}

	bool NavModule::warp(entt::entity entity, glm::vec3 position)
	{
		const Ref<Scene> scene = world_scene();
		return scene && m_Agents && m_Agents->warp(*scene, entity, position);
	}

	NavAgentState NavModule::state(entt::entity entity) const
	{
		return m_Agents ? m_Agents->state(entity) : NavAgentState::Idle;
	}

	float NavModule::remaining_distance(entt::entity entity) const
	{
		return m_Agents ? m_Agents->remaining_distance(entity) : 0.0f;
	}

	glm::vec3 NavModule::velocity(entt::entity entity) const
	{
		return m_Agents ? m_Agents->velocity(entity) : glm::vec3(0.0f);
	}

	u32 NavModule::add_listener(std::function<void(entt::entity, NavAgentState)> listener)
	{
		const u32 id = m_NextListener++;
		m_Listeners.emplace_back(id, std::move(listener));
		return id;
	}

	void NavModule::remove_listener(u32 id)
	{
		m_Listeners.erase(std::remove_if(m_Listeners.begin(), m_Listeners.end(), [id](const auto& entry) { return entry.first == id; }), m_Listeners.end());
	}
}
