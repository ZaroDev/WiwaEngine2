#pragma once

namespace wiwa
{
	// What the viewport overlay of a navmesh shows.
	struct NavOverlayOptions
	{
		bool Polys = true;     // the polygons, filled lines coloured by area
		bool Edges = true;     // every polygon edge
		bool Boundary = true;  // the edges that are the border of the walkable ground, bright
	};
}
