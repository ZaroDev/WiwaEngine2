#pragma once

#include <glm/glm.hpp>

#include "../asset_pipeline/nav_bake.h"

// Small navmesh inputs for checks that need something to walk on (a copy of the helpers of nav_smoke_test.cpp, which keeps its own).
namespace wiwa::nav_fixtures
{
	// A quad whose normal points away from `outward` is turned around; the triangles are (a, b, c) and (a, c, d).
	inline void add_quad(NavBakeInput& input, glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 outward)
	{
		if (glm::dot(glm::cross(b - a, c - a), outward) < 0.0f)
		{
			std::swap(b, d);
		}
		const u32 base = static_cast<u32>(input.Vertices.size());
		input.Vertices.insert(input.Vertices.end(), { a, b, c, d });
		input.Indices.insert(input.Indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
		input.TriangleAreas.push_back(static_cast<u8>(NavArea::Walkable));
		input.TriangleAreas.push_back(static_cast<u8>(NavArea::Walkable));
	}

	inline void add_box(NavBakeInput& input, glm::vec3 lo, glm::vec3 hi)
	{
		const glm::vec3 p[8] = {
			{ lo.x, lo.y, lo.z }, { hi.x, lo.y, lo.z }, { hi.x, hi.y, lo.z }, { lo.x, hi.y, lo.z },
			{ lo.x, lo.y, hi.z }, { hi.x, lo.y, hi.z }, { hi.x, hi.y, hi.z }, { lo.x, hi.y, hi.z } };
		add_quad(input, p[0], p[1], p[2], p[3], { 0, 0, -1 });
		add_quad(input, p[4], p[5], p[6], p[7], { 0, 0, 1 });
		add_quad(input, p[0], p[3], p[7], p[4], { -1, 0, 0 });
		add_quad(input, p[1], p[2], p[6], p[5], { 1, 0, 0 });
		add_quad(input, p[0], p[1], p[5], p[4], { 0, -1, 0 });
		add_quad(input, p[3], p[2], p[6], p[7], { 0, 1, 0 });
	}

	// A 20 x 20 m floor at y = 0, with a wall (1 m thick, 2 m high) across the middle that reaches halfZ either side of z = 0 (0: no wall).
	inline NavBakeInput floor_with_wall(float halfZ)
	{
		NavBakeInput input;
		add_quad(input, { -10, 0, -10 }, { -10, 0, 10 }, { 10, 0, 10 }, { 10, 0, -10 }, { 0, 1, 0 });
		if (halfZ > 0.0f)
		{
			add_box(input, { -0.5f, 0.0f, -halfZ }, { 0.5f, 2.0f, halfZ });
		}
		input.BoundsMin = { -10.0f, -1.0f, -10.0f };
		input.BoundsMax = { 10.0f, 4.0f, 10.0f };
		input.Build.CellSize = 0.25f;
		return input;
	}
}
