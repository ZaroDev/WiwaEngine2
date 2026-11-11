#include <pch.h>
#include "perception.h"

#include <algorithm>
#include <cmath>

#include <scene/components.h>
#include <scene/scene.h>

namespace wiwa
{
	namespace
	{
		bool finite(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

		// Writes a value to a key: a declared key of that type, or an undeclared key made on the spot. A key of another type is left alone.
		void put(Blackboard& board, const std::string& key, const BtValue& value)
		{
			if (key.empty())
			{
				return;
			}
			if (!board.set(key, value))
			{
				board.set_dynamic(key, value);
			}
		}

		bool team_listed(const std::string& list, const std::string& team)
		{
			size_t start = 0;
			while (start <= list.size())
			{
				size_t end = list.find(',', start);
				if (end == std::string::npos)
				{
					end = list.size();
				}
				size_t first = start;
				size_t last = end;
				while (first < last && std::isspace(static_cast<unsigned char>(list[first]))) { first++; }
				while (last > first && std::isspace(static_cast<unsigned char>(list[last - 1]))) { last--; }
				if (list.compare(first, last - first, team) == 0 && last > first)
				{
					return true;
				}
				start = end + 1;
			}
			return false;
		}

		bool wants(const Perceiver& perceiver, const Perceivable& target)
		{
			if (!target.Visible)
			{
				return false;
			}
			if (perceiver.TargetTeams.empty())
			{
				return perceiver.Team.empty() || target.Team != perceiver.Team; // everyone but its own team
			}
			return team_listed(perceiver.TargetTeams, target.Team);
		}
	}

	bool Perception::in_cone(glm::vec3 eye, glm::vec3 forward, float angleDegrees, glm::vec3 point)
	{
		if (angleDegrees >= 360.0f)
		{
			return true;
		}
		const glm::vec3 to = point - eye;
		const float length = glm::length(to);
		const float forwardLength = glm::length(forward);
		if (!(length > 1.0e-5f))
		{
			return true;
		}
		if (!(forwardLength > 0.0f) || !finite(to) || !finite(forward))
		{
			return false;
		}
		const float cosine = glm::clamp(glm::dot(to / length, forward / forwardLength), -1.0f, 1.0f);
		return cosine >= std::cos(glm::radians(std::max(angleDegrees, 0.0f) * 0.5f)) - 1.0e-6f;
	}

	void Perception::clear()
	{
		m_States.clear();
		m_RaysLastFrame = 0;
		m_DeferredLastFrame = 0;
		m_Cursor = 0;
	}

	const PerceptionResult* Perception::result(entt::entity perceiver) const
	{
		const auto it = m_States.find(perceiver);
		return it != m_States.end() ? &it->second.Result : nullptr;
	}

	void Perception::update(Scene& scene, double time, const SightRaycast& raycast, const std::vector<Noise>& noises, const BoardLookup& boardOf)
	{
		entt::registry& registry = scene.registry();
		m_RaysLastFrame = 0;
		m_DeferredLastFrame = 0;

		// Perceivers that went away.
		for (auto it = m_States.begin(); it != m_States.end();)
		{
			if (!registry.valid(it->first) || !registry.all_of<Perceiver>(it->first))
			{
				it = m_States.erase(it);
			}
			else
			{
				++it;
			}
		}

		// The perceivers in a stable order; the pass starts at the cursor, so the ones that were deferred last time go first.
		std::vector<entt::entity> perceivers;
		for (const entt::entity entity : registry.view<Perceiver, WorldTransform>())
		{
			perceivers.push_back(entity);
		}
		std::sort(perceivers.begin(), perceivers.end(), [](entt::entity a, entt::entity b) { return entt::to_integral(a) < entt::to_integral(b); });
		if (perceivers.empty())
		{
			return;
		}

		struct Target
		{
			entt::entity Entity{ entt::null };
			EntityGuid Guid{};
			glm::vec3 Aim{ 0.0f };
			glm::vec3 Position{ 0.0f };
		};

		const size_t count = perceivers.size();
		const size_t first = m_Cursor % count;
		size_t firstDeferred = count;
		for (size_t n = 0; n < count; n++)
		{
			const size_t index = (first + n) % count;
			const entt::entity entity = perceivers[index];
			const Perceiver& perceiver = registry.get<Perceiver>(entity);
			const glm::mat4& world = registry.get<WorldTransform>(entity).Matrix;
			State& state = m_States[entity];
			Blackboard* board = boardOf ? boardOf(entity) : nullptr;
			const glm::vec3 eye = glm::vec3(world * glm::vec4(perceiver.EyeOffset, 1.0f));
			const glm::vec3 forward = -glm::vec3(world[2]);
			const EntityGuid self = registry.all_of<Id>(entity) ? registry.get<Id>(entity).Guid : EntityGuid{};
			const bool usable = finite(eye) && finite(forward);

			// Hearing: every call, a noise lasts one call.
			if (usable && perceiver.HearingRadius > 0.0f)
			{
				const Noise* heard = nullptr;
				float nearest = 0.0f;
				for (const Noise& noise : noises)
				{
					const float reach = perceiver.HearingRadius * noise.Loudness;
					const float distance = glm::distance(eye, noise.Position);
					if (noise.Loudness > 0.0f && std::isfinite(distance) && distance <= reach && (heard == nullptr || distance < nearest))
					{
						heard = &noise;
						nearest = distance;
					}
				}
				if (heard != nullptr)
				{
					state.Result.HasHeard = true;
					state.Result.HeardPosition = heard->Position;
					state.Result.HeardKind = heard->Kind;
					if (board != nullptr)
					{
						put(*board, perceiver.HeardKey, heard->Position);
						put(*board, perceiver.HeardKindKey, heard->Kind);
					}
				}
			}

			// Sight: when due.
			if (time + 1.0e-9 < state.NextSense)
			{
				continue;
			}
			if (m_RaysLastFrame >= MAX_RAYS_PER_FRAME && m_RaysLastFrame > 0)
			{
				m_DeferredLastFrame++;
				firstDeferred = std::min(firstDeferred, n);
				continue; // over the budget: it stays due and goes first next frame
			}

			const double interval = std::max(static_cast<double>(perceiver.Interval), 0.0);
			bool seen = false;
			Target best;
			float bestDistance = 0.0f;
			if (usable && perceiver.SightRange > 0.0f)
			{
				const u32 mask = static_cast<u32>(perceiver.SightLayerMask);
				for (auto [targetEntity, perceivable, targetWorld] : registry.view<Perceivable, WorldTransform>().each())
				{
					if (targetEntity == entity || !wants(perceiver, perceivable))
					{
						continue;
					}
					Target target;
					target.Entity = targetEntity;
					target.Guid = registry.all_of<Id>(targetEntity) ? registry.get<Id>(targetEntity).Guid : EntityGuid{};
					target.Position = glm::vec3(targetWorld.Matrix[3]);
					target.Aim = glm::vec3(targetWorld.Matrix * glm::vec4(perceivable.AimOffset, 1.0f));
					if (!finite(target.Aim) || !finite(target.Position))
					{
						continue;
					}
					const float distance = glm::distance(eye, target.Aim);
					if (distance > perceiver.SightRange || !in_cone(eye, forward, perceiver.SightAngle, target.Aim))
					{
						continue;
					}
					if (seen && distance >= bestDistance)
					{
						continue; // not nearer than what is already seen: no ray needed
					}

					bool visible = true;
					if (distance > 1.0e-4f && raycast)
					{
						m_RaysLastFrame++;
						const glm::vec3 direction = (target.Aim - eye) / distance;
						for (const SightHit& hit : raycast(eye, direction, distance, mask))
						{
							if (hit.Key != target.Guid && hit.Key != self && hit.Distance < distance - 0.05f)
							{
								visible = false;
								break;
							}
						}
					}
					if (visible)
					{
						seen = true;
						best = target;
						bestDistance = distance;
					}
				}
			}

			state.NextSense = time + interval;
			state.Result.Sensed = true;
			state.Result.CanSee = seen;
			state.Result.Seen = seen ? best.Guid : EntityGuid{};
			if (seen)
			{
				state.Result.HasLastSeen = true;
				state.Result.LastSeenPosition = best.Position;
			}
			if (board != nullptr)
			{
				put(*board, perceiver.CanSeeKey, seen);
				put(*board, perceiver.SeenKey, state.Result.Seen);
				if (seen)
				{
					put(*board, perceiver.LastSeenKey, best.Position);
				}
			}
		}

		m_Cursor = firstDeferred < count ? static_cast<u32>((first + firstDeferred) % count) : 0;
	}
}
