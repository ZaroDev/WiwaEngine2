#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <math/math.h>
#include "asset_guid.h"

// A particle effect (.wfx, vfx.md): up to PARTICLE_MAX_EMITTERS emitters, each with a renderer and a fixed set of modules (spawn, start values,
// forces, values over the particle's life). Every number a module takes is a ParticleValue: a constant, a random range or a curve. Curves and
// the color gradient are baked into fixed-size sample tables when the file is read, so the GPU looks a table up (no keys, no search). The file
// is JSON; the cooked asset holds the same text, so reading it again gives the same tables.
namespace wiwa
{
	constexpr int WFX_VERSION = 1;
	constexpr u32 PARTICLE_MAX_EMITTERS = 8;
	constexpr u32 PARTICLE_CURVE_SAMPLES = 32;
	constexpr u32 PARTICLE_MAX_RIBBON_SEGMENTS = 64;
	constexpr u32 PARTICLE_DEFAULT_MAX_PARTICLES = 4096;
	constexpr u32 PARTICLE_LIMIT_MAX_PARTICLES = 65536; // per emitter

	enum class ParticleRenderer : u8 { Sprite, Ribbon, Mesh };
	enum class ParticleBlend : u8 { Additive, Alpha, Multiply }; // Alpha is premultiplied
	enum class ParticleAlign : u8 { Camera, Velocity, Axis, World };
	enum class ParticleCollision : u8 { None, Depth };
	enum class ParticleCollisionResponse : u8 { Bounce, Stick, Kill };
	enum class ParticleShape : u8 { Point, Sphere, Cone, Box };

	struct ParticleCurveKey
	{
		float Time = 0.0f;  // 0..1 of the particle's life
		float Value = 0.0f;

		bool operator==(const ParticleCurveKey&) const = default;
	};

	// A number of a module. Constant: A. Range: a random number between A and B per particle. Curve: Keys over the life (0..1), baked into Samples.
	struct ParticleValue
	{
		enum class Mode : u8 { Constant, Range, Curve };

		Mode Kind = Mode::Constant;
		float A = 0.0f;
		float B = 0.0f;
		std::vector<ParticleCurveKey> Keys{};
		std::array<float, PARTICLE_CURVE_SAMPLES> Samples{}; // a Curve's value at i / (PARTICLE_CURVE_SAMPLES - 1) of the life; filled by bake()

		static ParticleValue constant(float value);
		static ParticleValue range(float a, float b);
		static ParticleValue curve(std::vector<ParticleCurveKey> keys); // baked

		// Fills Samples from Keys (linear between keys, flat before the first and after the last). Does nothing for a Constant or a Range.
		void bake();
		// Constant: A. Range: A + (B - A) * t, with t the particle's random number. Curve: the baked table at t, the age as 0..1 (linear between samples).
		float evaluate(float t) const;

		bool operator==(const ParticleValue&) const = default;
	};

	struct ParticleColorKey
	{
		float Time = 0.0f;
		glm::vec4 Color{ 1.0f };

		bool operator==(const ParticleColorKey&) const = default;
	};

	struct ParticleBurst
	{
		float Time = 0.0f; // seconds after the emitter starts
		u32 Count = 10;

		bool operator==(const ParticleBurst&) const = default;
	};

	struct ParticleEmitterData
	{
		std::string Name = "Emitter";

		// What draws the particles. Material: the texture of sprites and ribbons (its base color, emissive and alpha), the surface of mesh particles
		// when set (else the mesh's own materials). Mesh: mesh particles only.
		ParticleRenderer Renderer = ParticleRenderer::Sprite;
		AssetRef Material{};
		AssetRef Mesh{};
		u32 SheetColumns = 1;
		u32 SheetRows = 1;
		ParticleAlign Align = ParticleAlign::Camera;
		ParticleBlend Blend = ParticleBlend::Alpha;
		float SoftDistance = 0.5f; // meters over which a particle fades out against the surface behind it; 0 = hard
		float Intensity = 1.0f;    // color multiplier (above 1 glows through bloom)
		bool Sorted = false;       // alpha blended: sort the particles back to front on the GPU
		bool Lit = false;          // reserved: lit sprites and ribbons
		u32 MaxParticles = PARTICLE_DEFAULT_MAX_PARTICLES;
		u32 RibbonSegments = 16;   // ribbons: points kept per trail
		float MinSegmentLength = 0.05f;

		ParticleCollision Collision = ParticleCollision::None;
		ParticleCollisionResponse Response = ParticleCollisionResponse::Bounce;
		float Restitution = 0.4f;

		// Spawn
		bool Looping = true;
		float Duration = 5.0f;     // seconds the emitter emits for (it loops after that when Looping)
		float StartDelay = 0.0f;
		ParticleValue Rate = ParticleValue::constant(10.0f); // particles per second
		std::vector<ParticleBurst> Bursts{};
		ParticleShape Shape = ParticleShape::Point;
		glm::vec3 ShapeSize{ 1.0f }; // Sphere: x = radius; Cone: x = radius of the base; Box: the extents
		float ConeAngle = 25.0f;     // degrees, Cone
		bool WorldSpace = true;      // particles stay where they were born (else they move with the emitter)

		// Start values
		ParticleValue Lifetime = ParticleValue::constant(2.0f);
		ParticleValue StartSpeed = ParticleValue::constant(1.0f);
		ParticleValue StartSize = ParticleValue::constant(0.2f);
		ParticleValue StartRotation = ParticleValue::constant(0.0f); // degrees
		glm::vec4 StartColor{ 1.0f };

		// Forces
		glm::vec3 Gravity{ 0.0f };
		float Drag = 0.0f;
		float NoiseStrength = 0.0f;
		float NoiseFrequency = 1.0f;

		// Over the life (a Curve, or a constant for none)
		ParticleValue SizeOverLife = ParticleValue::constant(1.0f);     // multiplies the start size
		ParticleValue RotationSpeed = ParticleValue::constant(0.0f);    // degrees per second
		ParticleValue AlphaOverLife = ParticleValue::constant(1.0f);    // multiplies the alpha
		ParticleValue FrameOverLife = ParticleValue::constant(0.0f);    // sprite sheet: 0..1 of the sheet
		std::vector<ParticleColorKey> ColorOverLife{};                  // empty: the start color stays; multiplies the start color
		std::array<glm::vec4, PARTICLE_CURVE_SAMPLES> ColorSamples{};   // baked from ColorOverLife (all ones when it is empty)

		// Fills the tables of every ParticleValue and ColorSamples; read_particle_file does it, call it after editing a curve or a gradient.
		void bake();

		bool operator==(const ParticleEmitterData&) const = default;
	};

	struct ParticleEffectData
	{
		std::string Name{};
		std::vector<ParticleEmitterData> Emitters{};

		bool operator==(const ParticleEffectData&) const = default;
	};

	// .wfx: JSON { "version", "name", "emitters": [ { "name", "renderer" (sprite|ribbon|mesh), "material", "mesh" ({guid, local}), "sheet": [columns, rows],
	// "align" (camera|velocity|axis|world), "blend" (additive|alpha|multiply), "softDistance", "intensity", "sorted", "lit", "maxParticles", "ribbon":
	// { "segments", "minSegmentLength" }, "collision": { "mode" (none|depth), "response" (bounce|stick|kill), "restitution" }, "spawn": { "looping", "duration",
	// "delay", "rate", "bursts": [ { "time", "count" } ], "shape" (point|sphere|cone|box), "shapeSize" (3), "coneAngle", "worldSpace" }, "start": { "lifetime",
	// "speed", "size", "rotation", "color" (4) }, "forces": { "gravity" (3), "drag", "noiseStrength", "noiseFrequency" }, "overLife": { "size", "rotationSpeed",
	// "alpha", "frame", "color": [ { "time", "color" (4) } ] } } ] }.
	// A value is a number (constant), [a, b] (random range) or { "curve": [ [time, value], ... ] } (keys over the life, times rising from 0 to 1).
	// Unknown keys are ignored. Every read is checked: a file that is not what it says gives std::nullopt and a reason, never a crash.
	std::string write_particle_file(const ParticleEffectData& effect);
	std::optional<ParticleEffectData> read_particle_file(std::string_view text, std::string& error);

	// The rules of an effect that can be played (the importer and Save enforce them): at most PARTICLE_MAX_EMITTERS emitters with unique names; per emitter
	// 1..PARTICLE_LIMIT_MAX_PARTICLES particles, a sheet of at most 256 cells, 2..PARTICLE_MAX_RIBBON_SEGMENTS ribbon segments, a positive Duration and
	// Lifetime, finite numbers, curve and gradient keys rising within 0..1. False, and the first problem in error.
	bool particle_effect_valid(const ParticleEffectData& effect, std::string& error);
}
