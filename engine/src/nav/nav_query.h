#pragma once

#include <optional>

#include <glm/glm.hpp>

#include "nav_data.h"
#include "nav_types.h"

namespace wiwa
{
	// Questions about a NavData: where to walk, what is under a point, whether a line is clear. One query object belongs to one thread at a time (it keeps
	// Detour's search state); the NavData it reads must outlive it. A point that is not a finite number, or that is not near the mesh, gives a failed
	// path / no answer, never a crash. All positions are in world space.
	class NavQuery
	{
	public:
		struct Impl;

		explicit NavQuery(const NavData& data);
		~NavQuery();

		NavQuery(const NavQuery&) = delete;
		NavQuery& operator=(const NavQuery&) = delete;

		// The straight path between the nearest mesh points to from and to (within extents of each). Partial: the walk ends where it comes closest to
		// `to` (the target is not reachable). Failed: no mesh near a point or no route; Corners is empty.
		NavPath find_path(glm::vec3 from, glm::vec3 to, const NavFilter& filter, glm::vec3 extents = { 2.0f, 4.0f, 2.0f }) const;

		// The point of the mesh nearest to `point` within extents.
		std::optional<glm::vec3> nearest_point(glm::vec3 point, glm::vec3 extents, const NavFilter& filter) const;

		// True, and where in `hit`, when the line from a to b leaves the mesh (a wall or an edge) before b. False for a clear line, and when a is not on the mesh.
		bool raycast(glm::vec3 a, glm::vec3 b, const NavFilter& filter, glm::vec3* hit = nullptr) const;

		// A random point of the mesh within radius of centre (which must be on the mesh).
		std::optional<glm::vec3> random_point_around(glm::vec3 centre, float radius, const NavFilter& filter) const;

		// Whether the point is on the mesh: within 0.5 m of it vertically and over a polygon.
		bool is_walkable(glm::vec3 point, const NavFilter& filter) const;

		// Seeds the generator of random_point_around (this thread's).
		static void seed_random(u32 seed);

	private:
		Scope<Impl> m_Impl{};
	};
}
