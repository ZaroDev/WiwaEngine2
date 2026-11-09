#pragma once

#include "components.h"
#include "scene.h"

// How long a decal lives. The times are in game seconds and run only while the game clock plays: in edit mode (and in a scene that is
// not the one being played) a decal shows at its full Opacity and never expires.
//
// A decal fades in over FadeInTime, stays for Lifetime, then fades out over FadeOutTime and is destroyed; so it lives for
// FadeInTime + Lifetime + FadeOutTime. Lifetime 0 is permanent (it still fades in).
namespace wiwa
{
	// Runtime state, not saved and not in the registry of components: how long the decal has been alive while the game plays. Added by
	// tick_decals to the decals that fade or expire.
	struct DecalAge
	{
		float Seconds = 0.0f;
	};

	// The 0..1 factor of the fades at an age (the opacity of the decal is Decal::Opacity times this).
	float decal_fade(const Decal& decal, float age);

	// The decal has lived through Lifetime and its fades and goes away. Never for Lifetime 0.
	bool decal_expired(const Decal& decal, float age);

	// Ages the decals of the scene by deltaTime seconds and destroys the expired ones. The caller runs it only while the game plays.
	void tick_decals(Scene& scene, float deltaTime);
}
