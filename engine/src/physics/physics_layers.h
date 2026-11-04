#pragma once

#include <array>
#include <string>

#include <math/math.h>

namespace wiwa
{
	constexpr u32 PHYSICS_LAYER_COUNT = 32;

	// The project's physics layers: up to 32 named layers and which pairs of them collide. Layer indices are what a Collider
	// stores. Jolt-free plain data (the project settings file and the physics module use it).
	struct PhysicsLayers
	{
		std::array<std::string, PHYSICS_LAYER_COUNT> Names{};
		std::array<u32, PHYSICS_LAYER_COUNT> Matrix{}; // Matrix[a] bit b: layer a collides with layer b

		// The default: layer 0 is "Default" and every layer collides with every other.
		PhysicsLayers();

		// True when both layers collide with each other (the matrix is kept symmetric, but both directions are tested, so a
		// hand-edited asymmetric matrix lets the pair through only when both sides agree). A layer past the last never collides.
		bool collides(u32 a, u32 b) const;

		// Sets the pair in both directions.
		void set_collides(u32 a, u32 b, bool value);
	};
}
