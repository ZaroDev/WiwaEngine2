#include <editor_common.h>
#include "nav_overlay.h"

#include <renderer/debug/debug_draw.h>

namespace wiwa
{
	namespace
	{
		constexpr size_t MAX_OVERLAY_LINES = DebugDraw::MAX_LINES - 4096; // room for the rest of the editor's lines
		constexpr size_t MAX_OVERLAY_TRIANGLES = DebugDraw::MAX_TRIANGLES - 1024;
	}

	glm::vec4 nav_area_color(u8 area, float alpha)
	{
		switch (static_cast<NavArea>(area))
		{
		case NavArea::Walkable: return { 0.20f, 0.80f, 0.35f, alpha };
		case NavArea::Mud: return { 0.60f, 0.40f, 0.20f, alpha };
		case NavArea::Water: return { 0.20f, 0.45f, 0.95f, alpha };
		case NavArea::Custom0: return { 0.90f, 0.30f, 0.80f, alpha };
		case NavArea::Custom1: return { 0.95f, 0.85f, 0.20f, alpha };
		case NavArea::Custom2: return { 0.25f, 0.85f, 0.90f, alpha };
		case NavArea::Custom3: return { 0.95f, 0.55f, 0.15f, alpha };
		case NavArea::Area8: return { 0.55f, 0.25f, 0.90f, alpha };
		case NavArea::Area9: return { 0.65f, 0.90f, 0.25f, alpha };
		case NavArea::Area10: return { 0.90f, 0.25f, 0.30f, alpha };
		case NavArea::Area11: return { 0.30f, 0.60f, 0.55f, alpha };
		case NavArea::Area12: return { 0.85f, 0.65f, 0.55f, alpha };
		case NavArea::Area13: return { 0.45f, 0.50f, 0.85f, alpha };
		case NavArea::Area14: return { 0.75f, 0.75f, 0.35f, alpha };
		case NavArea::Area15: return { 0.60f, 0.60f, 0.70f, alpha };
		default: return { 0.5f, 0.5f, 0.5f, alpha };
		}
	}

	NavOverlayShapes nav_overlay_shapes(const NavMeshView& view, const NavOverlayOptions& options, float lift)
	{
		NavOverlayShapes shapes;
		const glm::vec3 up(0.0f, lift, 0.0f);
		for (const NavViewPoly& poly : view.Polys)
		{
			if (options.Polys && poly.VertexCount >= 3 && shapes.Triangles.size() + poly.VertexCount - 2 <= MAX_OVERLAY_TRIANGLES)
			{
				const glm::vec3 first = view.Vertices[poly.FirstVertex] + up;
				for (u32 i = 1; i + 1 < poly.VertexCount; i++)
				{
					shapes.Triangles.push_back({ first, view.Vertices[poly.FirstVertex + i] + up, view.Vertices[poly.FirstVertex + i + 1] + up, nav_area_color(poly.Area, 0.30f) });
				}
			}

			if ((options.Edges || options.Boundary) && shapes.Lines.size() + poly.VertexCount <= MAX_OVERLAY_LINES)
			{
				for (u32 i = 0; i < poly.VertexCount; i++)
				{
					const u32 current = poly.FirstVertex + i;
					const u32 next = poly.FirstVertex + (i + 1) % poly.VertexCount;
					const bool boundary = view.BoundaryEdge[current] != 0;
					if (boundary && options.Boundary)
					{
						shapes.Lines.push_back({ view.Vertices[current] + up, view.Vertices[next] + up, { 1.0f, 0.95f, 0.85f, 1.0f } });
					}
					else if (options.Edges)
					{
						shapes.Lines.push_back({ view.Vertices[current] + up, view.Vertices[next] + up, nav_area_color(poly.Area, 0.55f) });
					}
				}
			}
		}
		return shapes;
	}

	void draw_nav_overlay(DebugDraw& draw, const NavOverlayShapes& shapes)
	{
		for (const NavOverlayTriangle& triangle : shapes.Triangles)
		{
			draw.triangle(triangle.A, triangle.B, triangle.C, triangle.Color, DebugDepth::Tested);
		}
		for (const NavOverlayLine& line : shapes.Lines)
		{
			draw.line(line.A, line.B, line.Color, DebugDepth::Tested, 1.5f);
		}
	}
}
