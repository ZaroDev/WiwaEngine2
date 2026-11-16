#ifndef PARTICLE_SIM_COMMON_HLSLI
#define PARTICLE_SIM_COMMON_HLSLI

#include "particle_common.hlsli"

// The constants of the particle compute passes (spawn, simulation, the sort). Mirrors SimConstants in particle_sim_pass.h. The matrices are the
// camera's of this frame as rasterised (jittered), so that a particle projects to the pixel whose depth the prepass wrote.
cbuffer SimConstants : register(b0)
{
	uint EmitterCount; // emitter records to look through (the highest live id + 1)
	uint TotalSpawn;
	uint SlotEnd;      // particle slots to age (one past the last live ring)
	uint FrameIndex;
	float DeltaTime;
	float Time;
	float Thickness;   // how far behind a surface a particle still counts as touching it (view depth)
	uint Collide;      // 1: there is a camera and a depth to collide with
	float4x4 ViewProjection;
	float4x4 InverseViewProjection;
	float4 Viewport;   // width, height, 1 / width, 1 / height
};

// Bit 31 of Particle.Emitter: the particle stuck to a surface and no longer moves.
static const uint PARTICLE_STUCK = 0x80000000u;

// Emitter.Spawn.z: bit 0 world space, bit 1 collides with the depth, bits 2-3 the response (0 bounce, 1 stick, 2 kill).
static const uint FLAG_WORLD_SPACE = 1u;
static const uint FLAG_COLLIDE = 2u;

#endif // PARTICLE_SIM_COMMON_HLSLI
