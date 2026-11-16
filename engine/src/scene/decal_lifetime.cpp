#include <pch.h>
#include "decal_lifetime.h"

#include <algorithm>
#include <vector>

namespace wiwa
{
	float decal_fade(const Decal& decal, float age)
	{
		float fade = 1.0f;
		if (decal.FadeInTime > 0.0f)
		{
			fade *= std::clamp(age / decal.FadeInTime, 0.0f, 1.0f);
		}
		if (decal.Lifetime > 0.0f)
		{
			const float outStart = std::max(decal.FadeInTime, 0.0f) + decal.Lifetime;
			if (decal.FadeOutTime > 0.0f)
			{
				fade *= std::clamp(1.0f - (age - outStart) / decal.FadeOutTime, 0.0f, 1.0f);
			}
			else if (age >= outStart)
			{
				fade = 0.0f;
			}
		}
		return fade;
	}

	bool decal_expired(const Decal& decal, float age)
	{
		return decal.Lifetime > 0.0f && age >= std::max(decal.FadeInTime, 0.0f) + decal.Lifetime + std::max(decal.FadeOutTime, 0.0f);
	}

	void tick_decals(Scene& scene, float deltaTime)
	{
		if (!(deltaTime > 0.0f))
		{
			return; // paused, or a step that is not a time (NaN)
		}

		entt::registry& registry = scene.registry();
		std::vector<entt::entity> expired;
		for (const auto [entity, decal] : registry.view<Decal>().each())
		{
			if (decal.Lifetime <= 0.0f && decal.FadeInTime <= 0.0f)
			{
				continue; // permanent and shown at once: nothing to age
			}

			DecalAge& age = registry.get_or_emplace<DecalAge>(entity);
			age.Seconds += deltaTime;
			if (decal_expired(decal, age.Seconds))
			{
				expired.push_back(entity);
			}
		}

		for (const entt::entity entity : expired)
		{
			scene.destroy(entity);
		}
	}
}
