#include <pch.h>
#include "particle_runtime.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace wiwa
{
	namespace
	{
		constexpr float MAX_STEP = 0.1f;
		constexpr float FINISH_MARGIN = 0.05f; // a little after the last particle's lifetime, for the frames it takes to leave the screen
		constexpr int MAX_CYCLES_PER_STEP = 8;

		// How many times the burst at `time` (seconds into the cycle) fires while the emitter's own time goes from `previous` (excluded) to `now` (included).
		u32 burst_crossings(float previous, float now, float time, float duration, bool loop)
		{
			if (time < 0.0f || time > duration)
			{
				return 0;
			}
			if (!loop)
			{
				return previous < time && time <= now ? 1u : 0u;
			}

			u32 count = 0;
			const int first = static_cast<int>(std::floor(std::max(previous, 0.0f) / duration));
			const int last = static_cast<int>(std::floor(now / duration));
			for (int cycle = first; cycle <= last && cycle < first + MAX_CYCLES_PER_STEP; cycle++)
			{
				const float at = static_cast<float>(cycle) * duration + time;
				count += previous < at && at <= now ? 1u : 0u;
			}
			return count;
		}
	}

	float particle_value_max(const ParticleValue& value)
	{
		switch (value.Kind)
		{
		case ParticleValue::Mode::Range:
			return std::max(value.A, value.B);
		case ParticleValue::Mode::Curve:
		{
			float most = 0.0f;
			for (const ParticleCurveKey& key : value.Keys)
			{
				most = std::max(most, key.Value);
			}
			return most;
		}
		case ParticleValue::Mode::Constant:
		default:
			return value.A;
		}
	}

	ParticleRuntime& particle_runtime_of(entt::registry& registry, entt::entity entity)
	{
		ParticleRuntime& runtime = registry.get_or_emplace<ParticleRuntime>(entity);
		if (!runtime.Started)
		{
			runtime.Started = true;
			runtime.Playing = registry.get<ParticleSystem>(entity).PlayOnStart;
			const EntityGuid guid = registry.all_of<Id>(entity) ? registry.get<Id>(entity).Guid : EntityGuid{};
			runtime.Seed = static_cast<u32>(guid.Hi ^ (guid.Hi >> 32) ^ guid.Lo ^ (guid.Lo >> 32)) | 1u;
		}
		return runtime;
	}

	void tick_particle_systems(Scene& scene, float deltaTime, const ParticleEffectLookup& effects)
	{
		entt::registry& registry = scene.registry();
		const float step = deltaTime > 0.0f ? std::min(deltaTime, MAX_STEP) : 0.0f; // NaN is not above 0
		std::vector<entt::entity> finished;

		for (const auto [entity, system] : registry.view<ParticleSystem>().each())
		{
			ParticleRuntime& runtime = particle_runtime_of(registry, entity);
			runtime.SpawnCount.fill(0);

			if (!runtime.Playing || runtime.Finished)
			{
				if (runtime.Finished && runtime.SelfDestruct)
				{
					finished.push_back(entity);
				}
				continue;
			}

			const ParticleEffectData* effect = effects ? effects(system.Effect) : nullptr;
			if (effect == nullptr)
			{
				continue; // not loaded yet: the system waits
			}

			const float delta = step * std::max(system.TimeScale, 0.0f);
			runtime.Time += delta;

			bool active = false;
			const size_t emitters = std::min<size_t>(effect->Emitters.size(), PARTICLE_MAX_EMITTERS);
			for (size_t i = 0; i < emitters; i++)
			{
				const ParticleEmitterData& emitter = effect->Emitters[i];
				const bool loop = emitter.Looping && system.Looping;
				const float local = runtime.Time - emitter.StartDelay;
				const float previous = runtime.PreviousLocal[i];

				// The time this emitter spent inside its emission window during the step.
				const float from = std::max(previous, 0.0f);
				const float to = loop ? std::max(local, 0.0f) : std::clamp(local, 0.0f, emitter.Duration);
				float emitting = std::max(to - from, 0.0f);
				if (local < 0.0f)
				{
					emitting = 0.0f;
				}

				u32 spawn = 0;
				if (emitting > 0.0f)
				{
					const float cycle = loop ? std::fmod(std::max(local, 0.0f), emitter.Duration) : std::min(local, emitter.Duration);
					const float rate = std::max(emitter.Rate.evaluate(emitter.Rate.Kind == ParticleValue::Mode::Curve ? cycle / emitter.Duration : 0.5f), 0.0f);
					runtime.Remainder[i] += rate * std::max(system.RateScale, 0.0f) * emitting;
					const float whole = std::floor(runtime.Remainder[i]);
					runtime.Remainder[i] -= whole;
					spawn = static_cast<u32>(std::min(whole, static_cast<float>(emitter.MaxParticles)));
				}
				if (local >= 0.0f)
				{
					const float burstTo = loop ? local : std::min(local, emitter.Duration);
					for (const ParticleBurst& burst : emitter.Bursts)
					{
						spawn += burst.Count * burst_crossings(previous, burstTo, burst.Time, emitter.Duration, loop);
					}
				}
				runtime.SpawnCount[i] = std::min(spawn, emitter.MaxParticles);
				if (local > previous)
				{
					runtime.PreviousLocal[i] = local; // only forward: a step of zero length must not use up the burst at time 0
				}

				// Done when it no longer emits and its last particle has died (by the longest life it can have).
				const float lastParticle = emitter.StartDelay + emitter.Duration + particle_value_max(emitter.Lifetime) + FINISH_MARGIN;
				active = active || loop || runtime.Time < lastParticle;
			}

			if (!active)
			{
				runtime.Finished = true;
				runtime.Playing = false;
				if (runtime.SelfDestruct)
				{
					finished.push_back(entity);
				}
			}
		}

		for (const entt::entity entity : finished)
		{
			scene.destroy(entity);
		}
	}
}
