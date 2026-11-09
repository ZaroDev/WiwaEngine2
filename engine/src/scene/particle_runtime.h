#pragma once

#include <array>
#include <functional>

#include <assets/particle_effect.h>
#include "components.h"
#include "scene.h"

// What a ParticleSystem does while the game plays (vfx.md): the main thread advances its clock and decides how many particles each emitter spawns this
// frame (from the rate, the bursts and the time that passed); the GPU does the rest. Nothing here touches the GPU, and in edit mode nothing runs.
namespace wiwa
{
	// Runtime state of a ParticleSystem: not reflected, not saved, not cloned. Added by tick_particle_systems on the first tick.
	struct ParticleRuntime
	{
		bool Started = false;       // PlayOnStart has been applied
		bool Playing = false;
		bool Finished = false;      // no emitter will spawn again and the last particle is gone (or its time is up)
		bool SelfDestruct = false;  // Vfx.spawn: the entity goes away when Finished
		bool Preview = false;      // plays in edit mode (the inspector's Play button, the VFX editor); the game clock is not needed
		float Time = 0.0f;          // seconds the system has played (TimeScale applied)
		u32 Seed = 0;               // the random numbers of the system on the GPU
		u32 ClearCount = 0;         // counts up when the particles of the system are to be discarded (Vfx.stop with clear, restart): the render thread starts its rings over
		std::array<float, PARTICLE_MAX_EMITTERS> Remainder{};     // the fraction of a particle carried to the next frame
		std::array<float, PARTICLE_MAX_EMITTERS> PreviousLocal{ -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f }; // each emitter's time at the last step
		std::array<u32, PARTICLE_MAX_EMITTERS> SpawnCount{};      // spawned this frame, per emitter: what the extractor sends

		// Starts over (the effect plays from its beginning).
		void restart()
		{
			ClearCount++;
			Playing = true;
			Finished = false;
			Time = 0.0f;
			Remainder.fill(0.0f);
			PreviousLocal.fill(-1.0f);
			SpawnCount.fill(0);
		}
	};

	// The runtime of a ParticleSystem; made, and started as PlayOnStart says (Playing, the seed), when the entity has none yet. The entity has a ParticleSystem.
	ParticleRuntime& particle_runtime_of(entt::registry& registry, entt::entity entity);

	// The effect a reference names, once it is loaded; null while it is not (the system waits).
	using ParticleEffectLookup = std::function<const ParticleEffectData*(const AssetRef&)>;

	// The most a value can be (the longest lifetime a particle can have).
	float particle_value_max(const ParticleValue& value);

	// Advances every ParticleSystem of the scene by deltaTime seconds and fills ParticleRuntime::SpawnCount. A step is at most 0.1 s (a stall does not
	// spawn a flood), and 0 or NaN does nothing. A finished system with SelfDestruct is destroyed. The caller runs it only while the game plays.
	// previewOnly: only the systems that have ParticleRuntime::Preview are advanced (edit mode); the others do nothing.
	void tick_particle_systems(Scene& scene, float deltaTime, const ParticleEffectLookup& effects, bool previewOnly = false);
}
