#ifndef PARTICLE_COMMON_HLSLI
#define PARTICLE_COMMON_HLSLI

// What the particle shaders share (vfx.md). The structs mirror ParticleGpu and EmitterGpu in particle_pool.h; the table buffer is float4 rows,
// PARTICLE_TABLE_ROWS of them per emitter id: 8 curves of 32 samples (8 rows each, 4 samples to a row), then the 32 colors of the gradient.

struct Particle
{
	float3 Position;
	float Age;
	float3 Velocity;
	float Lifetime;   // 0: the slot is dead
	float4 Color;
	float Size;
	float Rotation;
	uint Emitter;     // the emitter's id
	uint Seed;
};

struct Emitter
{
	float4 WorldC0;   // the system's matrix, as four columns
	float4 WorldC1;
	float4 WorldC2;
	float4 WorldC3;
	uint4 Ring;       // x = first slot, y = slot count, z = head, w = particles to spawn this frame
	uint4 Spawn;      // x = first spawn thread, y = seed, z = flags (1: world space), w = shape (0 point, 1 sphere, 2 cone, 3 box)
	float4 Shape;     // xyz = size, w = cone angle (radians)
	float4 Force;     // xyz = gravity, w = drag
	float4 Noise;     // x = strength, y = frequency, z = size scale, w = intensity
	float4 StartColor;
	float4 Values[8]; // (kind: 0 constant, 1 range, 2 curve; A; B; unused) of lifetime, speed, size, rotation, size over life, rotation speed, alpha over life, frame over life
	float4 Render;    // x = soft distance, y = sheet columns, z = sheet rows, w = alignment (0 camera, 1 velocity, 2 axis, 3 world)
	uint4 Render2;    // x = blend (0 additive, 1 alpha, 2 multiply)
};

static const uint PARTICLE_TABLE_ROWS = 96;
static const uint PARTICLE_COLOR_ROW = 64;
static const uint VALUE_LIFETIME = 0;
static const uint VALUE_SPEED = 1;
static const uint VALUE_SIZE = 2;
static const uint VALUE_ROTATION = 3;
static const uint VALUE_SIZE_OVER_LIFE = 4;
static const uint VALUE_ROTATION_SPEED = 5;
static const uint VALUE_ALPHA_OVER_LIFE = 6;
static const uint VALUE_FRAME_OVER_LIFE = 7;

uint PcgHash(uint v)
{
	uint state = v * 747796405u + 2891336453u;
	uint word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

// 0 to just under 1.
float Rand(inout uint state)
{
	state = PcgHash(state);
	return float(state & 0x00FFFFFFu) / 16777216.0;
}

float3 RandomUnitVector(inout uint state)
{
	const float z = Rand(state) * 2.0 - 1.0;
	const float angle = Rand(state) * 6.28318530718;
	const float r = sqrt(max(1.0 - z * z, 0.0));
	return float3(r * cos(angle), z, r * sin(angle));
}

// One sample (0..31) of curve `curve` (0..7) of emitter `id`.
float CurveSample(StructuredBuffer<float4> tables, uint id, uint curve, uint sample)
{
	return tables[id * PARTICLE_TABLE_ROWS + curve * 8 + (sample >> 2)][sample & 3];
}

// A module's number. t is the particle's random number for a range and the age (0..1) for a curve.
float EvalValue(StructuredBuffer<float4> tables, uint id, uint curve, float4 value, float t)
{
	if (value.x < 0.5)
	{
		return value.y;
	}
	if (value.x < 1.5)
	{
		return value.y + (value.z - value.y) * t;
	}

	const float position = saturate(t) * 31.0;
	const uint low = min((uint)position, 31u);
	const uint high = min(low + 1u, 31u);
	return lerp(CurveSample(tables, id, curve, low), CurveSample(tables, id, curve, high), position - (float)low);
}

float4 EvalColor(StructuredBuffer<float4> tables, uint id, float t)
{
	const float position = saturate(t) * 31.0;
	const uint low = min((uint)position, 31u);
	const uint high = min(low + 1u, 31u);
	const uint row = id * PARTICLE_TABLE_ROWS + PARTICLE_COLOR_ROW;
	return lerp(tables[row + low], tables[row + high], position - (float)low);
}

#endif // PARTICLE_COMMON_HLSLI
