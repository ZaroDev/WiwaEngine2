#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include <math/math.h>

namespace wiwa
{
	// What the project (not a scene) decides about how the renderer works: shadow reach and biases, vsync, uploads.
	struct RenderQuality
	{
		float ShadowDistance = 60.0f;     // the sun's cascades reach this far in front of the camera
		u32 ShadowCascades = 4;           // 1..4
		float ShadowSplitLambda = 0.8f;   // 0 = even splits, 1 = logarithmic
		float ShadowDepthBias = 0.0006f;  // in depth units of the shadow map, scaled up on surfaces turned away from the light
		float ShadowNormalBias = 1.5f;    // texels: receivers are moved along their normal by this much before they are looked up
		float SpotShadowBias = 0.0004f;
		bool Vsync = true;
		bool CopyQueue = true;
		u32 BloomMips = 6;                // 2..6: how many levels the glow spreads over
		u32 AoSlices = 2;                 // 1..4: directions searched per pixel
		u32 AoSteps = 8;                  // 4..16: samples per side of a slice
		bool Fxaa = false;                // off, like the other effects, until the project turns it on
		bool Taa = false;                 // temporal anti-aliasing; replaces FXAA when on (sanitized clears Fxaa)
		u32 ParticlePoolSize = 262144;    // 4096..4194304: particles the scene view's pool holds, shared by every emitter that plays (vfx.md)
		u32 ParticleMaxPerEmitter = 65536; // 64..65536: a ring never holds more than this, whatever the effect's own MaxParticles says
		float ParticleRateScale = 1.0f;   // 0..4: multiplies every emission rate (bursts are not scaled): the quality lever for weaker machines
		bool ParticleCollision = true;    // false: no particle collides with the depth buffer (it passes through, as if the effect said None)
		float LodPixelError = 1.5f;       // 0..16: how many pixels a lower level of detail may stray from the full mesh on screen; 0 draws every mesh at full detail

		bool operator==(const RenderQuality&) const = default;
	};

	// Finite values inside sane ranges (distance 1..1000, cascades 1..4, lambda 0..1, biases small).
	RenderQuality sanitized(RenderQuality quality);

	// The quality file of the project (JSON). A missing file gives nullopt and an empty error (use the defaults); a file that cannot be
	// read gives nullopt and the reason. Missing keys keep their defaults, unknown keys are ignored, values are sanitized.
	std::optional<RenderQuality> load_render_quality(const std::filesystem::path& path, std::string& error);
	bool save_render_quality(const std::filesystem::path& path, const RenderQuality& quality);
}
