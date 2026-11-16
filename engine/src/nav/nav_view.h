#pragma once

#include <vector>

#include <glm/glm.hpp>

#include <core/common.h>
#include "nav_data.h"
#include "nav_types.h"

namespace wiwa
{
	// One polygon of a navmesh: a run of Vertices (counter-clockwise from above), and its area.
	struct NavViewPoly
	{
		u32 FirstVertex = 0;
		u32 VertexCount = 0;
		u8 Area = static_cast<u8>(NavArea::Walkable);
	};

	// The polygons of a NavData as plain data, for drawing and for picking a point on the mesh (the editor's overlay and path test); no Detour type here.
	struct NavMeshView
	{
		std::vector<glm::vec3> Vertices{};
		std::vector<NavViewPoly> Polys{};
		// One per entry of Vertices: 1 when the edge from this vertex to the next one of its polygon has no neighbour (the border of the walkable ground:
		// a wall, a drop or the end of the mesh), 0 for an edge shared with another polygon (of its tile or of the next).
		std::vector<u8> BoundaryEdge{};
		glm::vec3 BoundsMin{ 0.0f };
		glm::vec3 BoundsMax{ 0.0f };

		bool empty() const { return Polys.empty(); }
	};

	// Every ground polygon of every tile.
	NavMeshView extract_navmesh_view(const NavData& data);
}
