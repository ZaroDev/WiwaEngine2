#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <math/math.h>

#include "physics_commands.h"

namespace wiwa
{
	struct RayHit
	{
		BodyKey Key{};
		glm::vec3 Point{ 0.0f };
		glm::vec3 Normal{ 0.0f };   // unit, pointing away from the body
		float Distance = 0.0f;      // from the origin (the cast's start for a shape cast), metres
	};

	// A read-only copy of the world as one step left it: for each body its pose and a shared pointer to its immutable shape. Queries run
	// against it on the main thread and never touch the live world. Bodies that do not collide (triggers, placeholders) are not in it.
	// A layer mask has bit n set for project layer n; ~0u asks for every layer. Directions need not be unit length (zero gives no hit).
	//
	// Cost: each query tests every body (no broad phase yet), so it is for scripts' occasional queries, not for thousands per step.
	class QuerySnapshot
	{
	public:
		struct Impl;

		explicit QuerySnapshot(std::unique_ptr<Impl> impl);
		~QuerySnapshot();
		QuerySnapshot(const QuerySnapshot&) = delete;
		QuerySnapshot& operator=(const QuerySnapshot&) = delete;

		// The closest hit within max_distance, or none.
		std::optional<RayHit> raycast(glm::vec3 origin, glm::vec3 direction, float maxDistance, u32 layerMask = ~0u) const;
		// Every hit (a body can be hit once per surface), nearest first.
		std::vector<RayHit> raycast_all(glm::vec3 origin, glm::vec3 direction, float maxDistance, u32 layerMask = ~0u) const;
		// The bodies that overlap the sphere or the box, in key order, each once.
		std::vector<BodyKey> overlap_sphere(glm::vec3 center, float radius, u32 layerMask = ~0u) const;
		std::vector<BodyKey> overlap_box(glm::vec3 center, glm::vec3 halfExtents, glm::quat rotation, u32 layerMask = ~0u) const;
		// Sweeps a shape (a primitive; Offset and Rotation place it at `from`) to `to` and gives the first body it meets.
		std::optional<RayHit> shape_cast(const ShapeDesc& shape, glm::vec3 from, glm::vec3 to, u32 layerMask = ~0u) const;

		size_t body_count() const;

	private:
		std::unique_ptr<Impl> m_Impl;
	};
}
