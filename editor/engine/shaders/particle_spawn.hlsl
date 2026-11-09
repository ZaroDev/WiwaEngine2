#include "Common.hlsli"
#include "particle_sim_common.hlsli"

// The spawn step (vfx.md): one thread per particle to spawn this frame, across every emitter. The thread finds its emitter by the range of spawn
// threads the emitter owns, takes the next slot of the emitter's ring (the oldest particle is overwritten when the ring is full) and writes the
// particle from the emitter's start modules.

StructuredBuffer<Emitter> uEmitters : register(t1);
StructuredBuffer<float4> uTables : register(t2);
RWStructuredBuffer<Particle> uParticles : register(u3);

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	const uint thread = id.x;
	if (thread >= TotalSpawn)
	{
		return;
	}

	uint e = 0;
	for (; e < EmitterCount; e++)
	{
		const uint start = uEmitters[e].Spawn.x;
		const uint count = uEmitters[e].Ring.w;
		if (count > 0 && thread >= start && thread < start + count)
		{
			break;
		}
	}
	if (e >= EmitterCount)
	{
		return;
	}

	const Emitter emitter = uEmitters[e];
	const uint index = thread - emitter.Spawn.x;
	const uint slot = emitter.Ring.x + (emitter.Ring.z + index) % emitter.Ring.y;
	uint rng = PcgHash(emitter.Spawn.y ^ (FrameIndex * 2654435761u) ^ (index * 40503u + 12345u));

	const float lifetime = max(EvalValue(uTables, e, VALUE_LIFETIME, emitter.Values[VALUE_LIFETIME], Rand(rng)), 0.001);
	const float speed = EvalValue(uTables, e, VALUE_SPEED, emitter.Values[VALUE_SPEED], Rand(rng));
	const float size = max(EvalValue(uTables, e, VALUE_SIZE, emitter.Values[VALUE_SIZE], Rand(rng)), 0.0) * emitter.Noise.z;
	const float rotation = radians(EvalValue(uTables, e, VALUE_ROTATION, emitter.Values[VALUE_ROTATION], Rand(rng)));

	float3 position = float3(0.0, 0.0, 0.0);
	float3 direction = float3(0.0, 1.0, 0.0);
	const uint shape = emitter.Spawn.w;
	if (shape == 0u)
	{
		direction = RandomUnitVector(rng);
	}
	else if (shape == 1u)
	{
		direction = RandomUnitVector(rng);
		position = direction * emitter.Shape.x * pow(Rand(rng), 1.0 / 3.0);
	}
	else if (shape == 2u)
	{
		const float baseAngle = Rand(rng) * 6.28318530718;
		const float baseRadius = sqrt(Rand(rng)) * emitter.Shape.x;
		position = float3(cos(baseAngle), 0.0, sin(baseAngle)) * baseRadius;
		const float cosTheta = lerp(1.0, cos(emitter.Shape.w), Rand(rng));
		const float sinTheta = sqrt(max(1.0 - cosTheta * cosTheta, 0.0));
		const float phi = Rand(rng) * 6.28318530718;
		direction = float3(sinTheta * cos(phi), cosTheta, sinTheta * sin(phi));
	}
	else
	{
		position = (float3(Rand(rng), Rand(rng), Rand(rng)) - 0.5) * emitter.Shape.xyz;
	}

	float3 velocity = direction * speed;
	if ((emitter.Spawn.z & 1u) != 0u)
	{
		// a world space emitter: the particle is born where the system is, and then it is on its own
		position = emitter.WorldC0.xyz * position.x + emitter.WorldC1.xyz * position.y + emitter.WorldC2.xyz * position.z + emitter.WorldC3.xyz;
		velocity = emitter.WorldC0.xyz * velocity.x + emitter.WorldC1.xyz * velocity.y + emitter.WorldC2.xyz * velocity.z;
	}

	Particle particle;
	particle.Position = position;
	particle.Age = 0.0;
	particle.Velocity = velocity;
	particle.Lifetime = lifetime;
	particle.Color = emitter.StartColor;
	particle.Size = size;
	particle.Rotation = rotation;
	particle.Emitter = e;
	particle.Seed = rng;
	uParticles[slot] = particle;
}
