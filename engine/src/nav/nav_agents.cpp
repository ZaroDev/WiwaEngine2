#include <pch.h>
#include "nav_agents.h"
#include "nav_data_impl.h"
#include "nav_project_settings.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <DetourCrowd.h>
#include <DetourNavMeshQuery.h>
#include <glm/gtc/quaternion.hpp>

#include <scene/components.h>
#include <scene/scene.h>

namespace wiwa
{
	namespace
	{
		constexpr float MAX_AGENT_RADIUS = 2.0f;
		constexpr float RETRY_SECONDS = 1.0f;

		struct ParamsKey
		{
			float Speed = 0, Acceleration = 0, Radius = 0, Height = 0, Separation = 0;
			i32 Avoidance = 0;
			u32 AreaMask = 0;

			bool operator==(const ParamsKey&) const = default;
		};

		ParamsKey key_of(const NavAgent& agent)
		{
			ParamsKey key;
			key.Speed = agent.Speed;
			key.Acceleration = agent.Acceleration;
			key.Radius = std::clamp(agent.Radius, 0.05f, MAX_AGENT_RADIUS);
			key.Height = agent.Height;
			key.Separation = agent.SeparationWeight;
			key.Avoidance = std::clamp(agent.AvoidanceQuality, 0, 3);
			key.AreaMask = static_cast<u32>(agent.AreaMask);
			return key;
		}

		bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

		struct Entry
		{
			int Index = -1;                 // in the crowd; -1: not in it (the mesh was not near, or the crowd is full)
			NavAgentState State = NavAgentState::Idle;
			glm::vec3 Target{ 0.0f };
			bool Partial = false;           // the walk ends short of the destination
			float Retry = 0.0f;             // seconds until another try to add the agent
			ParamsKey Last{};
			glm::vec3 Velocity{ 0.0f };
			bool WarnedFull = false;
			float Settled = 0.0f;           // seconds the agent has hardly moved while walking
		};
	}

	struct NavAgents::Impl
	{
		const NavData* Data = nullptr;
		NavSettings Settings{};
		dtCrowd* Crowd = nullptr;
		dtNavMeshQuery* Query = nullptr;
		std::unordered_map<entt::entity, Entry> Entries{};
		std::vector<u32> FilterMasks{};   // the area mask of each crowd filter slot

		~Impl()
		{
			dtFreeCrowd(Crowd);
			dtFreeNavMeshQuery(Query);
		}

		u32 crowd_size() const
		{
			u32 count = 0;
			for (const auto& [entity, entry] : Entries)
			{
				count += entry.Index >= 0 ? 1u : 0u;
			}
			return count;
		}

		NavFilter nav_filter(u32 mask) const
		{
			NavFilter filter;
			filter.AreaMask = mask;
			filter.AreaCost = Settings.AreaCost;
			return filter;
		}

		// The crowd's filter slot for an area mask: one per distinct mask (16 at most; more use the first).
		int filter_slot(u32 mask)
		{
			for (size_t i = 0; i < FilterMasks.size(); i++)
			{
				if (FilterMasks[i] == mask)
				{
					return static_cast<int>(i);
				}
			}
			if (FilterMasks.size() >= DT_CROWD_MAX_QUERY_FILTER_TYPE)
			{
				return 0;
			}
			const int slot = static_cast<int>(FilterMasks.size());
			FilterMasks.push_back(mask);
			fill_detour_filter(*Crowd->getEditableFilter(slot), nav_filter(mask));
			return slot;
		}

		dtCrowdAgentParams params_of(const ParamsKey& key)
		{
			dtCrowdAgentParams params{};
			params.radius = key.Radius;
			params.height = key.Height;
			params.maxAcceleration = key.Acceleration;
			params.maxSpeed = key.Speed;
			params.collisionQueryRange = key.Radius * 12.0f;
			params.pathOptimizationRange = key.Radius * 30.0f;
			params.separationWeight = key.Separation;
			params.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OPTIMIZE_VIS | DT_CROWD_OPTIMIZE_TOPO;
			if (key.Avoidance > 0)
			{
				params.updateFlags |= DT_CROWD_OBSTACLE_AVOIDANCE;
			}
			if (key.Separation > 0.0f)
			{
				params.updateFlags |= DT_CROWD_SEPARATION;
			}
			params.obstacleAvoidanceType = static_cast<unsigned char>(key.Avoidance);
			params.queryFilterType = static_cast<unsigned char>(filter_slot(key.AreaMask));
			return params;
		}

		bool add_to_crowd(Entry& entry, const glm::vec3& position, const NavAgent& agent)
		{
			const ParamsKey key = key_of(agent);
			dtQueryFilter filter;
			fill_detour_filter(filter, nav_filter(key.AreaMask));
			glm::vec3 extents = NAV_SNAP_EXTENTS;
			glm::vec3 point = position;
			dtPolyRef ref = 0;
			glm::vec3 snapped{ 0.0f };
			Query->findNearestPoly(&point.x, &extents.x, &filter, &ref, &snapped.x);
			if (ref == 0)
			{
				return false;
			}
			const dtCrowdAgentParams params = params_of(key);
			entry.Index = Crowd->addAgent(&snapped.x, &params);
			entry.Last = key;
			return entry.Index >= 0;
		}

		void remove_from_crowd(Entry& entry)
		{
			if (entry.Index >= 0)
			{
				Crowd->removeAgent(entry.Index);
				entry.Index = -1;
			}
		}

		// The Detour polygon and point for the mesh point nearest to `position`.
		bool snap(const glm::vec3& position, u32 areaMask, dtPolyRef& ref, glm::vec3& snapped)
		{
			dtQueryFilter filter;
			fill_detour_filter(filter, nav_filter(areaMask));
			glm::vec3 extents = NAV_SNAP_EXTENTS;
			glm::vec3 point = position;
			ref = 0;
			Query->findNearestPoly(&point.x, &extents.x, &filter, &ref, &snapped.x);
			return ref != 0;
		}
	};

	NavAgents::NavAgents(const NavData& data, const NavSettings& settings) : m_Impl(CreateScope<Impl>())
	{
		m_Impl->Data = &data;
		m_Impl->Settings = sanitized(settings);
		m_Impl->Query = dtAllocNavMeshQuery();
		if (m_Impl->Query != nullptr && dtStatusFailed(m_Impl->Query->init(data.impl().Mesh, 2048)))
		{
			dtFreeNavMeshQuery(m_Impl->Query);
			m_Impl->Query = nullptr;
		}
		m_Impl->Crowd = dtAllocCrowd();
		if (m_Impl->Crowd != nullptr && !m_Impl->Crowd->init(static_cast<int>(NAV_MAX_AGENTS), MAX_AGENT_RADIUS, data.impl().Mesh))
		{
			dtFreeCrowd(m_Impl->Crowd);
			m_Impl->Crowd = nullptr;
		}
		if (m_Impl->Crowd == nullptr || m_Impl->Query == nullptr)
		{
			WI_NAV_LOG(Error, "Navigation: cannot create the crowd");
			return;
		}

		// How hard an agent works to avoid the others, from none (quality 0 turns it off in the agent flags) to the best.
		const int divs[4] = { 5, 5, 7, 7 };
		const int depth[4] = { 1, 2, 2, 3 };
		for (int i = 0; i < 4; i++)
		{
			dtObstacleAvoidanceParams avoidance;
			std::memcpy(&avoidance, m_Impl->Crowd->getObstacleAvoidanceParams(0), sizeof(avoidance));
			avoidance.velBias = 0.5f;
			avoidance.adaptiveDivs = static_cast<unsigned char>(divs[i]);
			avoidance.adaptiveRings = 2;
			avoidance.adaptiveDepth = static_cast<unsigned char>(depth[i]);
			m_Impl->Crowd->setObstacleAvoidanceParams(i, &avoidance);
		}
		m_Impl->filter_slot(0xFFFFFFFFu); // slot 0 is "everything": the fallback
	}

	NavAgents::~NavAgents() = default;

	void NavAgents::update(Scene& scene, float dt)
	{
		Impl& impl = *m_Impl;
		if (impl.Crowd == nullptr)
		{
			return;
		}
		entt::registry& registry = scene.registry();

		// Agents whose component or entity is gone leave the crowd.
		for (auto it = impl.Entries.begin(); it != impl.Entries.end();)
		{
			if (!registry.valid(it->first) || !registry.all_of<NavAgent>(it->first))
			{
				impl.remove_from_crowd(it->second);
				it = impl.Entries.erase(it);
			}
			else
			{
				++it;
			}
		}

		// New agents, retries of those the mesh was not near, and changed parameters.
		for (auto [entity, agent, world] : registry.view<NavAgent, WorldTransform>().each())
		{
			Entry& entry = impl.Entries[entity];
			const glm::vec3 position(world.Matrix[3]);
			if (entry.Index < 0)
			{
				entry.Retry -= dt;
				if (entry.Retry <= 0.0f)
				{
					if (!finite(position) || !impl.add_to_crowd(entry, position, agent))
					{
						entry.Retry = RETRY_SECONDS;
						if (entry.Index < 0 && !entry.WarnedFull && impl.crowd_size() >= NAV_MAX_AGENTS)
						{
							entry.WarnedFull = true;
							WI_NAV_LOG(Warning, "Navigation: the crowd is full ({0} agents), an agent is left out", NAV_MAX_AGENTS);
						}
					}
				}
			}
			else
			{
				const ParamsKey key = key_of(agent);
				if (!(key == entry.Last))
				{
					const dtCrowdAgentParams params = impl.params_of(key);
					impl.Crowd->updateAgentParameters(entry.Index, &params);
					entry.Last = key;
				}
			}
		}

		impl.Crowd->update(dt, nullptr);

		for (auto& [entity, entry] : impl.Entries)
		{
			if (entry.Index < 0)
			{
				continue;
			}
			const dtCrowdAgent* crowdAgent = impl.Crowd->getAgent(entry.Index);
			if (crowdAgent == nullptr || !crowdAgent->active)
			{
				continue;
			}
			const glm::vec3 position(crowdAgent->npos[0], crowdAgent->npos[1], crowdAgent->npos[2]);
			entry.Velocity = glm::vec3(crowdAgent->vel[0], crowdAgent->vel[1], crowdAgent->vel[2]);

			const NavAgent& agent = registry.get<NavAgent>(entity);
			if (entry.State == NavAgentState::Moving)
			{
				const bool failed = crowdAgent->targetState == DT_CROWDAGENT_TARGET_FAILED;
				const float arrival = std::max(agent.StoppingDistance, 0.1f);
				const float distance = glm::distance(position, entry.Target);
				// Agents that share a destination cannot overlap: those that cannot get closer, near it, have arrived.
				entry.Settled = glm::length(glm::vec2(entry.Velocity.x, entry.Velocity.z)) < 0.15f ? entry.Settled + dt : 0.0f;
				const bool crowded = entry.Settled > 1.0f && distance <= std::max(arrival, key_of(agent).Radius * 4.0f);
				if (failed || distance <= arrival || crowded)
				{
					impl.Crowd->resetMoveTarget(entry.Index);
					entry.State = (failed || entry.Partial) ? NavAgentState::NoPath : NavAgentState::Arrived;
					if (OnState)
					{
						OnState(entity, entry.State);
					}
				}
			}

			if (!agent.DriveTransform)
			{
				continue;
			}

			// The crowd works in world space; a parented entity gets the local values that put it there.
			Transform& transform = registry.get<Transform>(entity);
			glm::mat4 parentWorld(1.0f);
			if (const Hierarchy* hierarchy = registry.try_get<Hierarchy>(entity); hierarchy != nullptr && hierarchy->Parent != entt::null && registry.valid(hierarchy->Parent))
			{
				if (const WorldTransform* parent = registry.try_get<WorldTransform>(hierarchy->Parent))
				{
					parentWorld = parent->Matrix;
				}
			}
			const glm::mat4 toLocal = glm::inverse(parentWorld);
			transform.Position = glm::vec3(toLocal * glm::vec4(position, 1.0f));

			const glm::vec2 heading(entry.Velocity.x, entry.Velocity.z);
			if (glm::length(heading) > 0.1f)
			{
				const float yaw = std::atan2(-heading.x, -heading.y); // the entity faces -Z
				const glm::quat worldRotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
				const glm::quat parentRotation = glm::quat_cast(glm::mat3(glm::normalize(parentWorld[0]), glm::normalize(parentWorld[1]), glm::normalize(parentWorld[2])));
				const glm::quat wanted = glm::inverse(parentRotation) * worldRotation;
				transform.Rotation = glm::slerp(transform.Rotation, wanted, 1.0f - std::exp(-12.0f * dt));
			}
		}
	}

	bool NavAgents::set_destination(Scene& scene, entt::entity entity, glm::vec3 destination)
	{
		Impl& impl = *m_Impl;
		entt::registry& registry = scene.registry();
		if (impl.Crowd == nullptr || !registry.valid(entity) || !registry.all_of<NavAgent, WorldTransform>(entity) || !finite(destination))
		{
			return false;
		}

		Entry& entry = impl.Entries[entity];
		const NavAgent& agent = registry.get<NavAgent>(entity);
		if (entry.Index < 0 && !impl.add_to_crowd(entry, glm::vec3(registry.get<WorldTransform>(entity).Matrix[3]), agent))
		{
			entry.State = NavAgentState::NoPath;
			return false;
		}

		const u32 mask = static_cast<u32>(agent.AreaMask);
		dtPolyRef targetRef = 0;
		glm::vec3 target{ 0.0f };
		const dtCrowdAgent* crowdAgent = impl.Crowd->getAgent(entry.Index);
		dtPolyRef startRef = 0;
		glm::vec3 start{ crowdAgent->npos[0], crowdAgent->npos[1], crowdAgent->npos[2] };
		glm::vec3 startPoint{ 0.0f };
		if (!impl.snap(destination, mask, targetRef, target) || !impl.snap(start, mask, startRef, startPoint))
		{
			impl.Crowd->resetMoveTarget(entry.Index);
			entry.State = NavAgentState::NoPath;
			return false;
		}

		// Whether the route reaches the target; if not, the walk goes to the nearest reachable point.
		dtQueryFilter filter;
		fill_detour_filter(filter, impl.nav_filter(mask));
		dtPolyRef polys[256];
		int polyCount = 0;
		if (dtStatusFailed(impl.Query->findPath(startRef, targetRef, &startPoint.x, &target.x, &filter, polys, &polyCount, 256)) || polyCount == 0)
		{
			impl.Crowd->resetMoveTarget(entry.Index);
			entry.State = NavAgentState::NoPath;
			return false;
		}
		entry.Partial = polys[polyCount - 1] != targetRef;
		if (entry.Partial)
		{
			bool over = false;
			glm::vec3 reachable{ 0.0f };
			if (dtStatusFailed(impl.Query->closestPointOnPoly(polys[polyCount - 1], &target.x, &reachable.x, &over)))
			{
				entry.State = NavAgentState::NoPath;
				return false;
			}
			targetRef = polys[polyCount - 1];
			target = reachable;
		}

		if (!impl.Crowd->requestMoveTarget(entry.Index, targetRef, &target.x))
		{
			entry.State = NavAgentState::NoPath;
			return false;
		}
		entry.Target = target;
		entry.State = NavAgentState::Moving;
		return true;
	}

	void NavAgents::stop(entt::entity entity)
	{
		const auto it = m_Impl->Entries.find(entity);
		if (it != m_Impl->Entries.end() && it->second.Index >= 0)
		{
			m_Impl->Crowd->resetMoveTarget(it->second.Index);
			it->second.State = NavAgentState::Idle;
		}
	}

	bool NavAgents::warp(Scene& scene, entt::entity entity, glm::vec3 position)
	{
		Impl& impl = *m_Impl;
		entt::registry& registry = scene.registry();
		if (impl.Crowd == nullptr || !registry.valid(entity) || !registry.all_of<NavAgent>(entity) || !finite(position))
		{
			return false;
		}
		Entry& entry = impl.Entries[entity];
		impl.remove_from_crowd(entry);
		entry.State = NavAgentState::Idle;
		const NavAgent& agent = registry.get<NavAgent>(entity);
		if (!impl.add_to_crowd(entry, position, agent))
		{
			return false;
		}
		const dtCrowdAgent* crowdAgent = impl.Crowd->getAgent(entry.Index);
		if (agent.DriveTransform && registry.all_of<Transform>(entity))
		{
			registry.get<Transform>(entity).Position = glm::vec3(crowdAgent->npos[0], crowdAgent->npos[1], crowdAgent->npos[2]);
		}
		return true;
	}

	NavAgentState NavAgents::state(entt::entity entity) const
	{
		const auto it = m_Impl->Entries.find(entity);
		return it != m_Impl->Entries.end() ? it->second.State : NavAgentState::Idle;
	}

	float NavAgents::remaining_distance(entt::entity entity) const
	{
		const auto it = m_Impl->Entries.find(entity);
		if (it == m_Impl->Entries.end() || it->second.Index < 0 || it->second.State != NavAgentState::Moving)
		{
			return 0.0f;
		}
		const dtCrowdAgent* crowdAgent = m_Impl->Crowd->getAgent(it->second.Index);
		return glm::distance(glm::vec3(crowdAgent->npos[0], crowdAgent->npos[1], crowdAgent->npos[2]), it->second.Target);
	}

	glm::vec3 NavAgents::velocity(entt::entity entity) const
	{
		const auto it = m_Impl->Entries.find(entity);
		return it != m_Impl->Entries.end() ? it->second.Velocity : glm::vec3(0.0f);
	}

	u32 NavAgents::active_count() const
	{
		u32 count = 0;
		for (const auto& [entity, entry] : m_Impl->Entries)
		{
			count += entry.Index >= 0 ? 1u : 0u;
		}
		return count;
	}
}
