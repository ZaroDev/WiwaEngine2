#include "Common.hlsli"
#include "particle_sim_common.hlsli"

// The first step of sorting an emitter's particles back to front (vfx.md): one entry per slot of the ring, padded up to a power of two: the view depth
// of the particle (-1 for a dead slot and for the padding, which sort last) and the slot. particle_sort_step.hlsl then sorts the entries.
struct SortConstantsData
{
	uint Offset;  // first entry of this emitter's part of the sort buffer
	uint Count;   // slots of the ring
	uint Padded;  // entries: Count up to a power of two
	uint Emitter; // the emitter's id
};
PUSH_CONSTANTS(SortConstantsData, Sort, b1);

StructuredBuffer<Emitter> uEmitters : register(t1);
StructuredBuffer<Particle> uParticles : register(t2);
RWStructuredBuffer<uint2> uSort : register(u3);

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	const uint i = id.x;
	if (i >= Sort.Padded)
	{
		return;
	}

	float key = -1.0;
	if (i < Sort.Count)
	{
		const Emitter emitter = uEmitters[Sort.Emitter];
		const Particle particle = uParticles[emitter.Ring.x + i];
		if (particle.Lifetime > 0.0 && particle.Age < particle.Lifetime)
		{
			float3 position = particle.Position;
			if ((emitter.Spawn.z & FLAG_WORLD_SPACE) == 0u)
			{
				position = emitter.WorldC0.xyz * position.x + emitter.WorldC1.xyz * position.y + emitter.WorldC2.xyz * position.z + emitter.WorldC3.xyz;
			}
			key = mul(ViewProjection, float4(position, 1.0)).w;
		}
	}
	uSort[Sort.Offset + i] = uint2(asuint(key), i);
}
