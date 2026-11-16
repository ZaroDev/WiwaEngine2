#pragma once

#include <functional>
#include <unordered_map>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <core/common.h>
#include "nav_data.h"
#include "nav_types.h"

namespace wiwa
{
	class Scene;

	enum class NavAgentState : u8
	{
		Idle,     // no destination
		Moving,   // walking to one
		Arrived,  // got there (until the next destination)
		NoPath    // the destination cannot be reached: the walk ended at the nearest reachable point, or never began
	};

	// The NavAgent components of one scene as the agents of a DetourCrowd: they are added when the component is seen, removed when it or its entity
	// goes, steered to their destinations around each other, and their transforms follow. Main thread only. Created for one NavData (which must
	// outlive it) by the NavModule.
	class NavAgents
	{
	public:
		struct Impl;

		NavAgents(const NavData& data, const NavSettings& settings);
		~NavAgents();

		NavAgents(const NavAgents&) = delete;
		NavAgents& operator=(const NavAgents&) = delete;

		// Called when an agent arrives or finds no path (not for Idle / Moving).
		std::function<void(entt::entity, NavAgentState)> OnState{};

		// Makes the agents of the scene's components, steps the crowd by dt seconds, and moves the transforms of agents with DriveTransform.
		void update(Scene& scene, float dt);

		// Starts a walk to the point of the mesh nearest to `destination` (within 2 x 4 x 2 m). False, with the state NoPath, if there is none, the
		// entity has no usable agent, or no route leads even partway. A route that stops short is walked to its end, then the state is NoPath.
		bool set_destination(Scene& scene, entt::entity entity, glm::vec3 destination);
		void stop(entt::entity entity);
		// Puts the agent at the point of the mesh nearest to `position`; it stops walking. False if the mesh is not near it.
		bool warp(Scene& scene, entt::entity entity, glm::vec3 position);

		NavAgentState state(entt::entity entity) const;
		// Straight distance to the destination; 0 without one.
		float remaining_distance(entt::entity entity) const;
		glm::vec3 velocity(entt::entity entity) const;
		// Agents that stand in the crowd now.
		u32 active_count() const;

	private:
		Scope<Impl> m_Impl{};
	};

	constexpr u32 NAV_MAX_AGENTS = 128;
}
