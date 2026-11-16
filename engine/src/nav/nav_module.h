#pragma once

#include <functional>
#include <unordered_map>

#include <core/common.h>
#include <core/module.h>
#include <core/scene_follower.h>

#include <assets/asset_manager.h>
#include <assets/handle.h>

#include "nav_agents.h"
#include "nav_data.h"
#include "nav_mesh_asset.h"
#include "nav_project_settings.h"
#include "nav_query.h"

namespace wiwa
{
	class Scene;

	// Owns the navigation mesh of the scene that plays, a query object over it and the crowd of its agents. Main thread only; the render and physics
	// threads never touch it. A world (the mesh and the agents) exists while a scene plays: with auto start (the default, a game and the editor's play
	// mode) the module follows the SceneModule's active scene while the game clock plays and stops when another replaces it (the editor's Stop shows
	// the edit scene again). Without auto start (checks) start_world / start_world_with_mesh drive it.
	class NavModule final : public IModule
	{
	public:
		NavModule() = default;
		virtual ~NavModule() override = default;

		virtual void init() override;
		virtual void shutdown() override;
		virtual void update(float deltaTime) override;

		virtual UpdateFlags get_update_flags() const override { return UpdateFlags::UPDATE; }
		virtual std::string get_name() const override { return "NavModule"; }
		virtual int get_priority() const override { return ModulePriority::Nav; }

		// The area costs the agents and the queries use. A game reads the packed ones in init(); the editor applies nav.wsettings. Takes effect at the next world.
		void set_settings(const NavSettings& settings) { m_Settings = sanitized(settings); }
		const NavSettings& settings() const { return m_Settings; }

		void set_auto_start(bool autoStart) { m_Follow.set_auto_start(autoStart); }

		// Starts a world for the scene: the mesh of its first enabled NavMeshVolume that has a baked asset loads in the background (queries fail, and a
		// destination is kept for later, until it is there). A second enabled volume is ignored with a warning; none, or one without a baked mesh, logs
		// one warning and leaves the world without a mesh. True; false only if the scene is null.
		bool start_world(Ref<Scene> scene);
		// The same with a mesh at hand (checks, the editor's path tool).
		bool start_world_with_mesh(Ref<Scene> scene, const NavMeshData& mesh);
		// Destroys the agents and the mesh. Does nothing when no world runs.
		void stop_world();
		Ref<Scene> world_scene() const { return m_Follow.scene(); }
		// Makes the world for `scene` now when the module follows the active scene, the game plays and the world is another one (or none): a script asks
		// before the module's first tick (a destination in the first frame) and in the frame a scene is replaced. True when a world for that scene runs.
		bool ensure_world(const Ref<Scene>& scene, bool playing);

		bool has_navmesh() const { return m_Data != nullptr; }
		// Enabled volumes of the world's scene beyond the first (their meshes are not used).
		u32 ignored_volumes() const { return m_IgnoredVolumes; }
		bool loading() const { return m_Loading; }
		// Null without a mesh.
		const NavQuery* query() const { return m_Query.get(); }
		// The filter of an area mask with the project's costs.
		NavFilter filter_for(u32 areaMask) const;

		// One frame for the given active scene and clock state; dt is the game clock's seconds. update() calls it with the engine's; a check can call
		// it without an Engine.
		void tick(const Ref<Scene>& active, bool playing, float dt);

		// Agents (the entity needs a NavAgent and must be in the world's scene). A destination before the mesh has loaded is kept and applied when
		// it has; the call then returns true. See NavAgents for the rest.
		bool set_destination(entt::entity entity, glm::vec3 destination);
		void stop(entt::entity entity);
		bool warp(entt::entity entity, glm::vec3 position);
		NavAgentState state(entt::entity entity) const;
		float remaining_distance(entt::entity entity) const;
		glm::vec3 velocity(entt::entity entity) const;
		u32 active_agent_count() const { return m_Agents ? m_Agents->active_count() : 0; }

		// Told when an agent arrives or finds no path; returns an id for remove_listener.
		u32 add_listener(std::function<void(entt::entity, NavAgentState)> listener);
		void remove_listener(u32 id);

	private:
		void make_data(const NavMeshData& mesh);
		void poll_loading();
		void warn_once(const char* message);

		NavSettings m_Settings{};
		SceneFollower m_Follow{};
		bool m_Loading = false;
		Handle<NavMeshData> m_Handle{};
		Scope<NavData> m_Data{};
		Scope<NavQuery> m_Query{};
		Scope<NavAgents> m_Agents{};
		std::unordered_map<entt::entity, glm::vec3> m_Queued{}; // destinations asked for while the mesh loaded
		bool m_Warned = false;
		u32 m_IgnoredVolumes = 0;

		std::vector<std::pair<u32, std::function<void(entt::entity, NavAgentState)>>> m_Listeners{};
		u32 m_NextListener = 1;
	};
}
