#include "material_common.hlsli"
#include "particle_common.hlsli"

// Sprite particles (vfx.md): one draw per emitter, one instance per slot of its ring (a dead slot is moved outside the clip volume). The emitter is
// DrawConstants.InstanceIndex, the material DrawConstants.MaterialSlot. Sets: frame (0), the material's (1), this pass's own (2).
// The depth is read through a shader view and the particle is drawn with no depth attachment (a read-only attachment that is also sampled is not
// clean on Vulkan and D3D12): behind a surface it fades to nothing, near one it fades by the emitter's soft distance.
VK_BINDING(36, 2) StructuredBuffer<Particle> uParticles : register(t36);
VK_BINDING(37, 2) StructuredBuffer<Emitter> uEmitters : register(t37);
VK_BINDING(38, 2) StructuredBuffer<float4> uTables : register(t38);
VK_BINDING(39, 2) Texture2D<float> uSceneDepth : register(t39);
VK_BINDING(40, 2) StructuredBuffer<uint2> uSort : register(t40); // (view depth, slot) of a sorted emitter, farthest first: Render2.y - 1 is where its part starts

struct SpriteVertexOutput
{
	float4 Position : SV_Position;
	float2 Uv : TEXCOORD0;
	float4 Color : COLOR0;
	float ViewDepth : TEXCOORD1;
};

static const float2 SPRITE_CORNERS[6] = { float2(-0.5, -0.5), float2(0.5, -0.5), float2(-0.5, 0.5), float2(-0.5, 0.5), float2(0.5, -0.5), float2(0.5, 0.5) };

SpriteVertexOutput VSMain(uint vertex : SV_VertexID, uint instance : SV_InstanceID)
{
	const uint id = DrawConstants.InstanceIndex;
	const Emitter emitter = uEmitters[id];
	uint slot = instance;
	bool valid = true;
	if (emitter.Render2.y != 0u)
	{
		// sorted: the entry says which slot is drawn at this place (the padding of the sort has slots past the ring)
		slot = uSort[emitter.Render2.y - 1u + instance].y;
		valid = slot < emitter.Ring.y;
	}
	const Particle particle = uParticles[emitter.Ring.x + (valid ? slot : 0u)];

	SpriteVertexOutput output = (SpriteVertexOutput)0;
	if (!valid || particle.Lifetime <= 0.0 || particle.Age >= particle.Lifetime)
	{
		output.Position = float4(0.0, 0.0, 2.0, 1.0); // outside the clip volume: nothing is drawn
		return output;
	}

	const float life = saturate(particle.Age / particle.Lifetime);
	const float size = particle.Size * max(EvalValue(uTables, id, VALUE_SIZE_OVER_LIFE, emitter.Values[VALUE_SIZE_OVER_LIFE], life), 0.0);

	float3 position = particle.Position;
	float3 velocity = particle.Velocity;
	if ((emitter.Spawn.z & 1u) == 0u)
	{
		// a local space emitter: the particle moves with the system
		position = emitter.WorldC0.xyz * position.x + emitter.WorldC1.xyz * position.y + emitter.WorldC2.xyz * position.z + emitter.WorldC3.xyz;
		velocity = emitter.WorldC0.xyz * velocity.x + emitter.WorldC1.xyz * velocity.y + emitter.WorldC2.xyz * velocity.z;
	}

	const float2 corner = SPRITE_CORNERS[vertex];
	const float3 toCamera = normalize(CameraPosition.xyz - position);

	float3 right = float3(View[0][0], View[0][1], View[0][2]);
	float3 up = float3(View[1][0], View[1][1], View[1][2]);
	float2 scale = float2(size, size);
	float angle = particle.Rotation;
	const uint align = (uint)emitter.Render.w;
	if (align == 1u)
	{
		// stretched along the velocity
		const float speed = length(velocity);
		if (speed > 1e-4)
		{
			right = velocity / speed;
			up = normalize(cross(toCamera, right));
			scale.x = size * (1.0 + speed * 0.1);
			angle = 0.0;
		}
	}
	else if (align == 2u)
	{
		// upright: turns about the vertical axis to face the camera
		up = float3(0.0, 1.0, 0.0);
		right = normalize(cross(up, toCamera));
	}
	else if (align == 3u)
	{
		// flat in the world, facing up
		right = float3(1.0, 0.0, 0.0);
		up = float3(0.0, 0.0, 1.0);
	}

	const float c = cos(angle);
	const float s = sin(angle);
	const float2 turned = float2(corner.x * c - corner.y * s, corner.x * s + corner.y * c) * scale;
	const float3 world = position + right * turned.x + up * turned.y;

	// the sprite sheet cell of this moment of the life
	const float columns = max(emitter.Render.y, 1.0);
	const float rows = max(emitter.Render.z, 1.0);
	const float cells = columns * rows;
	const float cell = min(floor(saturate(EvalValue(uTables, id, VALUE_FRAME_OVER_LIFE, emitter.Values[VALUE_FRAME_OVER_LIFE], life)) * cells), cells - 1.0);
	const float2 uv = float2(corner.x + 0.5, 0.5 - corner.y);
	output.Uv = (float2(fmod(cell, columns), floor(cell / columns)) + uv) / float2(columns, rows);

	float4 color = particle.Color * EvalColor(uTables, id, life);
	color.a *= max(EvalValue(uTables, id, VALUE_ALPHA_OVER_LIFE, emitter.Values[VALUE_ALPHA_OVER_LIFE], life), 0.0);
	color.rgb *= emitter.Noise.w;
	output.Color = color;

	output.Position = mul(ViewProjection, float4(world, 1.0));
	output.ViewDepth = output.Position.w;
	return output;
}

float4 PSMain(SpriteVertexOutput input) : SV_Target0
{
	const Emitter emitter = uEmitters[DrawConstants.InstanceIndex];
	const MaterialRecord material = uMaterials[DrawConstants.MaterialSlot];
	const float4 texel = uBaseColor.Sample(uMaterialSampler, input.Uv) * material.BaseColor;
	const float4 color = input.Color * texel;

	// the surface behind: nothing is drawn through it, and the sprite fades where it meets it
	float fade = 1.0;
	const float depth = uSceneDepth.Load(int3(int2(input.Position.xy), 0));
	if (depth < 1.0)
	{
		const float2 uv = input.Position.xy * ViewportSize.zw;
		const float4 world = mul(InverseViewProjection, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0));
		const float surfaceDepth = -mul(View, float4(world.xyz / world.w, 1.0)).z;
		const float gap = surfaceDepth - input.ViewDepth;
		fade = emitter.Render.x > 0.0 ? saturate(gap / emitter.Render.x) : (gap >= 0.0 ? 1.0 : 0.0);
	}

	const float alpha = color.a * fade;
	if (emitter.Render2.x == 2u)
	{
		return float4(lerp(float3(1.0, 1.0, 1.0), color.rgb, alpha), 1.0); // multiply: the pixel is multiplied by this
	}
	return float4(color.rgb * alpha, alpha); // additive and alpha blended premultiplied: the pipeline's blend state says which
}
