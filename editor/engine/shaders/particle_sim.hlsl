#include "Common.hlsli"
#include "particle_common.hlsli"

// The simulation step (vfx.md): ages every live particle of the pool and moves it by the forces of its emitter. A slot is live while its age is below
// its lifetime (a zeroed slot is dead). The over-life numbers (size, alpha, color, frame) are read when the particle is drawn.
cbuffer SimConstants : register(b0)
{
	uint EmitterCount;
	uint TotalSpawn;
	uint SlotEnd;
	uint FrameIndex;
	float DeltaTime;
	float Time;
	float2 Padding;
};

StructuredBuffer<Emitter> uEmitters : register(t1);
StructuredBuffer<float4> uTables : register(t2);
RWStructuredBuffer<Particle> uParticles : register(u3);

[numthreads(64, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
	if (id.x >= SlotEnd)
	{
		return;
	}

	Particle particle = uParticles[id.x];
	if (particle.Lifetime <= 0.0 || particle.Age >= particle.Lifetime)
	{
		return;
	}

	const Emitter emitter = uEmitters[particle.Emitter];
	particle.Age += DeltaTime;
	const float life = saturate(particle.Age / particle.Lifetime);

	particle.Velocity += emitter.Force.xyz * DeltaTime;
	particle.Velocity *= exp(-emitter.Force.w * DeltaTime);
	if (emitter.Noise.x != 0.0)
	{
		const float3 q = particle.Position * emitter.Noise.y + float((particle.Seed & 1023u)) * 0.37 + Time * 0.5;
		const float3 wind = float3(sin(q.y * 1.7 + q.z), sin(q.z * 1.3 + q.x * 0.7), sin(q.x * 1.9 + q.y * 0.3));
		particle.Velocity += wind * emitter.Noise.x * DeltaTime;
	}
	particle.Position += particle.Velocity * DeltaTime;
	particle.Rotation += radians(EvalValue(uTables, particle.Emitter, VALUE_ROTATION_SPEED, emitter.Values[VALUE_ROTATION_SPEED], life)) * DeltaTime;

	uParticles[id.x] = particle;
}
