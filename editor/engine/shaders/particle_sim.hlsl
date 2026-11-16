#include "Common.hlsli"
#include "particle_sim_common.hlsli"

// The simulation step (vfx.md): ages every live particle of the pool and moves it by the forces of its emitter. A slot is live while its age is below
// its lifetime (a zeroed slot is dead). The over-life numbers (size, alpha, color, frame) are read when the particle is drawn.
//
// Collision with the depth (emitters with the collide flag, world space only): the step runs after this frame's depth prepass, so a particle that ends the
// step just behind the surface of its pixel (less than Thickness behind it) has touched it. It then bounces off the surface (the normal of the prepass),
// sticks to it or dies. A particle off the screen, in the sky or far behind a surface (the wall of another room) passes through.
StructuredBuffer<Emitter> uEmitters : register(t1);
StructuredBuffer<float4> uTables : register(t2);
RWStructuredBuffer<Particle> uParticles : register(u3);
Texture2D<float> uDepth : register(t4);
Texture2D<float2> uNormal : register(t5);

[numthreads(WI_PARTICLE_GROUP_SIZE, 1, 1)]
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

	const uint emitterId = particle.Emitter & ~PARTICLE_STUCK;
	const Emitter emitter = uEmitters[emitterId];
	particle.Age += DeltaTime;
	if ((particle.Emitter & PARTICLE_STUCK) != 0u)
	{
		uParticles[id.x] = particle; // stuck to a surface: it only ages
		return;
	}

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
	particle.Rotation += radians(EvalValue(uTables, emitterId, VALUE_ROTATION_SPEED, emitter.Values[VALUE_ROTATION_SPEED], life)) * DeltaTime;

	const uint flags = emitter.Spawn.z;
	if (Collide != 0u && (flags & (FLAG_WORLD_SPACE | FLAG_COLLIDE)) == (FLAG_WORLD_SPACE | FLAG_COLLIDE))
	{
		const float4 clip = mul(ViewProjection, float4(particle.Position, 1.0));
		if (clip.w > 0.0)
		{
			const float2 ndc = clip.xy / clip.w;
			if (abs(ndc.x) < 1.0 && abs(ndc.y) < 1.0)
			{
				const int2 pixel = int2(min(uint2((ndc.x * 0.5 + 0.5) * Viewport.x, (0.5 - ndc.y * 0.5) * Viewport.y), uint2(Viewport.xy) - 1u));
				const float depth = uDepth.Load(int3(pixel, 0));
				if (depth < 1.0)
				{
					// the point of the surface in this pixel, and how far behind it the particle is
					const float2 center = (float2(pixel) + 0.5) * Viewport.zw;
					const float4 surface4 = mul(InverseViewProjection, float4(center.x * 2.0 - 1.0, 1.0 - center.y * 2.0, depth, 1.0));
					const float3 surface = surface4.xyz / surface4.w;
					const float gap = clip.w - mul(ViewProjection, float4(surface, 1.0)).w;
					if (gap > 0.0 && gap < Thickness)
					{
						const float3 normal = OctahedralDecode(uNormal.Load(int3(pixel, 0)));
						const float along = dot(particle.Velocity, normal);
						if (along < 0.0)
						{
							const uint response = (flags >> 2u) & 3u;
							if (response == 2u)
							{
								particle.Age = particle.Lifetime; // dies on touching
							}
							else
							{
								particle.Position = surface + normal * 0.002;
								if (response == 1u)
								{
									particle.Velocity = float3(0.0, 0.0, 0.0);
									particle.Emitter |= PARTICLE_STUCK;
								}
								else
								{
									const float restitution = asfloat(emitter.Render2.w);
									particle.Velocity -= normal * along * (1.0 + restitution); // the part into the surface turns around, scaled
								}
							}
						}
					}
				}
			}
		}
	}

	uParticles[id.x] = particle;
}
