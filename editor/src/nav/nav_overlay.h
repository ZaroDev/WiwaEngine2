#pragma once

#include <vector>

#include <glm/glm.hpp>

#include <core/common.h>
#include <nav/nav_view.h>

namespace wiwa
{
	class DebugDraw;

	// What the viewport overlay of a navmesh shows.
	struct NavOverlayOptions
	{
		bool Polys = true;     // the polygons, filled, coloured by area
		bool Edges = true;     // every polygon edge, coloured by area
		bool Boundary = true;  // the edges that are the border of the walkable ground (a wall, a drop), bright
	};

	struct NavOverlayLine
	{
		glm::vec3 A{ 0.0f };
		glm::vec3 B{ 0.0f };
		glm::vec4 Color{ 1.0f };
	};

	struct NavOverlayTriangle
	{
		glm::vec3 A{ 0.0f };
		glm::vec3 B{ 0.0f };
		glm::vec3 C{ 0.0f };
		glm::vec4 Color{ 1.0f };
	};

	// What an overlay draws, as plain data (so a check can count it without a renderer).
	struct NavOverlayShapes
	{
		std::vector<NavOverlayLine> Lines{};
		std::vector<NavOverlayTriangle> Triangles{};
	};

	// Walkable green, Mud brown, Water blue, the four custom areas distinct; the colour the editor shows an area in.
	glm::vec4 nav_area_color(u8 area, float alpha);

	// The shapes for a view and the options, lifted `lift` metres so they do not fight with the ground. At most DebugDraw's caps worth of each (the
	// rest is left out, the nearest polygons first is not promised).
	NavOverlayShapes nav_overlay_shapes(const NavMeshView& view, const NavOverlayOptions& options, float lift = 0.05f);

	void draw_nav_overlay(DebugDraw& draw, const NavOverlayShapes& shapes);
}
