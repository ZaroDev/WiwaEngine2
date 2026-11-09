#pragma once

#include <optional>
#include <vector>

#include <nvrhi/nvrhi.h>

#include <assets/particle_effect.h>
#include <math/math.h>
#include "../resources/range_allocator.h"

// The GPU memory of the particles (vfx.md). One pool for every system in the scene: each emitter owns a ring of slots in one big buffer. A spawn
// writes the next slots of the ring (the oldest particle is overwritten when the ring is full, so there is no free list), the simulation ages every
// slot of the pool, and a slot whose age has reached its lifetime is dead (a zeroed slot is dead). The CPU never reads a particle back.
//
// Render thread only. The structs below are mirrored in particle_common.hlsli.
namespace wiwa
{
	struct ParticleGpu
	{
		glm::vec3 Position{ 0.0f };
		float Age = 0.0f;
		glm::vec3 Velocity{ 0.0f };
		float Lifetime = 0.0f; // 0: the slot is dead
		glm::vec4 Color{ 1.0f };
		float Size = 0.0f;
		float Rotation = 0.0f; // radians
		u32 Emitter = 0;       // the emitter's id (its record in the emitter table)
		u32 Seed = 0;          // this particle's own random numbers
	};
	static_assert(sizeof(ParticleGpu) == 64);

	// One record per live emitter (EmitterGpu in particle_common.hlsli): where it is, where its ring is, and every module's numbers. The number of a
	// ParticleValue is (kind: 0 constant, 1 range, 2 curve; A; B; unused); a curve's table is in the table buffer.
	struct EmitterGpu
	{
		glm::vec4 World[4]{};   // the system's matrix, as four columns
		glm::uvec4 Ring{};      // x = first slot, y = slot count, z = head (where the next spawn goes), w = particles to spawn this frame
		glm::uvec4 Spawn{};     // x = first spawn thread of this emitter, y = seed, z = flags (1: world space), w = shape (0 point, 1 sphere, 2 cone, 3 box)
		glm::vec4 Shape{};      // xyz = shape size, w = cone angle in radians
		glm::vec4 Force{};      // xyz = gravity, w = drag
		glm::vec4 Noise{};      // x = strength, y = frequency, z = size scale, w = intensity
		glm::vec4 StartColor{}; // the start color times the system's tint
		glm::vec4 Values[8]{};  // lifetime, speed, size, rotation, size over life, rotation speed, alpha over life, frame over life
		glm::vec4 Render{};     // x = soft distance, y = sheet columns, z = sheet rows, w = alignment (0 camera, 1 velocity, 2 axis, 3 world)
		glm::uvec4 Render2{};   // x = blend (0 additive, 1 alpha, 2 multiply)
	};
	static_assert(sizeof(EmitterGpu) == 320);

	// float4 rows per emitter id in the table buffer: 8 curves of 32 samples (8 rows each), then the 32 colors of the gradient.
	constexpr u32 PARTICLE_TABLE_CURVES = 8;
	constexpr u32 PARTICLE_TABLE_ROWS = PARTICLE_TABLE_CURVES * (PARTICLE_CURVE_SAMPLES / 4) + PARTICLE_CURVE_SAMPLES;

	struct ParticleAllocation
	{
		u32 Id = 0;       // the emitter's record in the emitter table, and its rows in the table buffer
		u32 Base = 0;     // first slot of the ring
		u32 Capacity = 0; // slots in the ring
	};

	// What the render pass draws this frame (filled by the simulation pass, in drawing order).
	struct ParticleDraw
	{
		u32 Id = 0;
		u32 Base = 0;
		u32 Capacity = 0;
		AssetRef Material{};
		ParticleBlend Blend = ParticleBlend::Alpha;
	};

	class ParticlePool
	{
	public:
		static constexpr u32 MAX_EMITTERS = 1024;
		static constexpr u32 DEFAULT_CAPACITY = 262144; // particles (16 MB)

		// How many particles the pool holds. A pool that exists is dropped (the frames still using it keep it alive) and made again when it is next needed.
		void set_capacity(u32 capacity);
		u32 capacity() const { return m_Capacity; }

		// Makes the buffers when they do not exist yet. False when they cannot be made.
		bool ensure(nvrhi::IDevice* device);
		bool ready() const { return m_Particles != nullptr; }
		// Counts up whenever the buffers are dropped: a pass that kept allocations, binding sets or pipelines of the pool starts over.
		u32 generation() const { return m_Generation; }
		void shutdown();

		// A ring of `count` slots and an emitter id; nothing when the pool is full or has no free id. The slots hold what they held before: zero them.
		std::optional<ParticleAllocation> allocate(u32 count);
		void release(const ParticleAllocation& allocation);

		u32 used() const { return static_cast<u32>(m_Ranges.used()); }
		// One past the last slot of any live ring, and one past the highest live emitter id: how far the simulation and the emitter upload reach.
		u32 slot_end() const;
		u32 id_end() const;

		nvrhi::IBuffer* particles() const { return m_Particles; }
		nvrhi::IBuffer* emitters() const { return m_Emitters; }
		nvrhi::IBuffer* tables() const { return m_Tables; }

		// For the render pass; cleared and filled every frame by the simulation pass.
		std::vector<ParticleDraw> Draws{};

	private:
		u32 m_Capacity = DEFAULT_CAPACITY;
		u32 m_Generation = 0;
		RangeAllocator m_Ranges{};
		std::vector<ParticleAllocation> m_Live{};
		std::vector<bool> m_IdUsed = std::vector<bool>(MAX_EMITTERS, false);
		nvrhi::BufferHandle m_Particles{};
		nvrhi::BufferHandle m_Emitters{};
		nvrhi::BufferHandle m_Tables{};
	};
}
