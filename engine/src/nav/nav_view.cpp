#include <pch.h>
#include "nav_view.h"
#include "nav_data_impl.h"

#include <algorithm>
#include <limits>

namespace wiwa
{
	NavMeshView extract_navmesh_view(const NavData& data)
	{
		NavMeshView view;
		const dtNavMesh* mesh = data.impl().Mesh;
		if (mesh == nullptr)
		{
			return view;
		}

		glm::vec3 lo(std::numeric_limits<float>::max());
		glm::vec3 hi(std::numeric_limits<float>::lowest());
		for (int t = 0; t < mesh->getMaxTiles(); t++)
		{
			const dtMeshTile* tile = mesh->getTile(t);
			if (tile == nullptr || tile->header == nullptr)
			{
				continue;
			}
			for (int p = 0; p < tile->header->polyCount; p++)
			{
				const dtPoly& poly = tile->polys[p];
				if (poly.getType() != DT_POLYTYPE_GROUND || poly.vertCount < 3)
				{
					continue;
				}

				NavViewPoly out;
				out.FirstVertex = static_cast<u32>(view.Vertices.size());
				out.VertexCount = poly.vertCount;
				out.Area = poly.getArea();
				for (int v = 0; v < poly.vertCount; v++)
				{
					const float* position = &tile->verts[poly.verts[v] * 3];
					const glm::vec3 vertex(position[0], position[1], position[2]);
					view.Vertices.push_back(vertex);
					lo = glm::min(lo, vertex);
					hi = glm::max(hi, vertex);

					// An edge with no neighbour is the border; an edge to the next tile counts as one only if no link leads across it.
					bool boundary = poly.neis[v] == 0;
					if ((poly.neis[v] & DT_EXT_LINK) != 0)
					{
						boundary = true;
						for (unsigned int link = poly.firstLink; link != DT_NULL_LINK; link = tile->links[link].next)
						{
							if (tile->links[link].edge == v)
							{
								boundary = false;
								break;
							}
						}
					}
					view.BoundaryEdge.push_back(boundary ? 1 : 0);
				}
				view.Polys.push_back(out);
			}
		}

		if (!view.Polys.empty())
		{
			view.BoundsMin = lo;
			view.BoundsMax = hi;
		}
		return view;
	}
}
