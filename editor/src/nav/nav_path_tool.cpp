#include <editor_common.h>
#include "nav_path_tool.h"

#include <cmath>
#include <limits>

#include <renderer/debug/debug_draw.h>

namespace wiwa
{
	namespace
	{
		// Moller-Trumbore: the distance along the ray to the triangle, or a negative number for a miss. Both faces count.
		float ray_triangle(const glm::vec3& origin, const glm::vec3& direction, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c)
		{
			const glm::vec3 edge1 = b - a;
			const glm::vec3 edge2 = c - a;
			const glm::vec3 p = glm::cross(direction, edge2);
			const float determinant = glm::dot(edge1, p);
			if (std::abs(determinant) < 1.0e-9f)
			{
				return -1.0f;
			}
			const float inverse = 1.0f / determinant;
			const glm::vec3 t = origin - a;
			const float u = glm::dot(t, p) * inverse;
			if (u < 0.0f || u > 1.0f)
			{
				return -1.0f;
			}
			const glm::vec3 q = glm::cross(t, edge1);
			const float v = glm::dot(direction, q) * inverse;
			if (v < 0.0f || u + v > 1.0f)
			{
				return -1.0f;
			}
			return glm::dot(edge2, q) * inverse;
		}
	}

	std::optional<glm::vec3> NavPathTool::pick(const NavMeshView& view, glm::vec3 rayOrigin, glm::vec3 rayDirection)
	{
		const float length = glm::length(rayDirection);
		if (!(length > 0.0f) || !std::isfinite(length) || !std::isfinite(rayOrigin.x) || !std::isfinite(rayOrigin.y) || !std::isfinite(rayOrigin.z))
		{
			return std::nullopt;
		}
		const glm::vec3 direction = rayDirection / length;

		float nearest = std::numeric_limits<float>::max();
		for (const NavViewPoly& poly : view.Polys)
		{
			for (u32 i = 1; i + 1 < poly.VertexCount; i++)
			{
				const float distance = ray_triangle(rayOrigin, direction, view.Vertices[poly.FirstVertex], view.Vertices[poly.FirstVertex + i], view.Vertices[poly.FirstVertex + i + 1]);
				if (distance >= 0.0f && distance < nearest)
				{
					nearest = distance;
				}
			}
		}
		if (nearest == std::numeric_limits<float>::max())
		{
			return std::nullopt;
		}
		return rayOrigin + direction * nearest;
	}

	void NavPathTool::clear()
	{
		m_Start.reset();
		m_End.reset();
		m_Path = {};
	}

	void NavPathTool::recompute(const NavQuery& query, const NavFilter& filter)
	{
		m_Path = {};
		if (m_Start && m_End)
		{
			m_Path = query.find_path(*m_Start, *m_End, filter);
		}
	}

	void NavPathTool::draw(DebugDraw& draw) const
	{
		const glm::vec3 lift(0.0f, 0.15f, 0.0f);
		if (m_Start)
		{
			draw.sphere(*m_Start + lift, 0.25f, { 0.2f, 1.0f, 0.3f, 1.0f }, DebugDepth::AlwaysOnTop);
		}
		if (m_End)
		{
			draw.sphere(*m_End + lift, 0.25f, { 1.0f, 0.3f, 0.2f, 1.0f }, DebugDepth::AlwaysOnTop);
		}
		const glm::vec4 color = m_Path.Status == NavPathStatus::Complete ? glm::vec4(1.0f, 1.0f, 0.2f, 1.0f) : glm::vec4(1.0f, 0.5f, 0.1f, 1.0f);
		for (size_t i = 1; i < m_Path.Corners.size(); i++)
		{
			draw.line(m_Path.Corners[i - 1] + lift, m_Path.Corners[i] + lift, color, DebugDepth::AlwaysOnTop, 3.0f);
		}
		for (const glm::vec3& corner : m_Path.Corners)
		{
			draw.sphere(corner + lift, 0.1f, color, DebugDepth::AlwaysOnTop);
		}
	}
}
