#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <core/common.h>
#include <scene/entity_guid.h>
#include "blackboard.h"

namespace wiwa
{
	class Scene;

	// What a sight ray met: the body (an entity guid) and how far along the ray.
	struct SightHit
	{
		EntityGuid Key{};
		float Distance = 0.0f;
	};

	// Every body the ray crosses within the distance (nearest first is not needed). Production: the physics query snapshot; a check gives its own.
	using SightRaycast = std::function<std::vector<SightHit>(glm::vec3 origin, glm::vec3 direction, float maxDistance, u32 layerMask)>;

	struct Noise
	{
		glm::vec3 Position{ 0.0f };
		float Loudness = 1.0f;
		std::string Kind{};
	};

	// What one Perceiver senses, as of its last sensing pass.
	struct PerceptionResult
	{
		bool Sensed = false;           // a sight pass has run
		bool CanSee = false;
		EntityGuid Seen{};             // the nearest visible target; the null guid when none
		bool HasLastSeen = false;
		glm::vec3 LastSeenPosition{ 0.0f }; // kept after sight is lost
		bool HasHeard = false;
		glm::vec3 HeardPosition{ 0.0f };
		std::string HeardKind{};
	};

	// Sight and hearing for the Perceiver components of a scene (spec: behaviour-trees design). Sight runs per perceiver every `Interval` seconds
	// (the first pass at once), costing one raycast per target in range and cone; at most MAX_RAYS_PER_FRAME rays are spent in a frame, the perceivers
	// that did not get theirs stay due and go first next frame. Hearing is checked every call (a noise lasts one call). A perceiver with a controller
	// has its results written to that controller's blackboard (the key names are fields of the Perceiver; an empty name switches an output off).
	// Main thread only.
	class Perception
	{
	public:
		static constexpr u32 MAX_RAYS_PER_FRAME = 64;

		// The blackboard to write the results of the perceiver's entity to; null: none (the results are only kept).
		using BoardLookup = std::function<Blackboard*(entt::entity)>;

		void update(Scene& scene, double time, const SightRaycast& raycast, const std::vector<Noise>& noises, const BoardLookup& boardOf);
		void clear();

		const PerceptionResult* result(entt::entity perceiver) const;
		u32 rays_last_frame() const { return m_RaysLastFrame; }
		// How many perceivers were due but did not get a pass in the last call (over the ray budget).
		u32 deferred_last_frame() const { return m_DeferredLastFrame; }

		// Whether the point is inside the cone from `eye` along `forward` of that full angle in degrees (360 or more: always). A point at the eye is inside.
		static bool in_cone(glm::vec3 eye, glm::vec3 forward, float angleDegrees, glm::vec3 point);

	private:
		struct State
		{
			double NextSense = 0.0;
			PerceptionResult Result{};
		};

		std::unordered_map<entt::entity, State> m_States{};
		u32 m_RaysLastFrame = 0;
		u32 m_DeferredLastFrame = 0;
		u32 m_Cursor = 0;
	};
}
