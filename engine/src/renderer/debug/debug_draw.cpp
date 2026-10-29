#include <pch.h>
#include "debug_draw.h"

#include <algorithm>
#include <cmath>

namespace wiwa
{
	namespace
	{
		constexpr u32 CIRCLE_SEGMENTS = 32;
		constexpr float PI = 3.14159265358979f;
		constexpr float FADED_ALPHA = 0.25f;

		bool finite(float value) { return std::isfinite(value); }
		bool finite(const glm::vec3& v) { return finite(v.x) && finite(v.y) && finite(v.z); }
		bool finite(const glm::vec4& v) { return finite(v.x) && finite(v.y) && finite(v.z) && finite(v.w); }
		bool finite(const glm::mat4& m) { return finite(m[0]) && finite(m[1]) && finite(m[2]) && finite(m[3]); }
		bool positive(float value) { return finite(value) && value > 0.0f; }

		u32 pack(const glm::vec4& color, float alphaScale = 1.0f)
		{
			auto channel = [](float v) { return static_cast<u32>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
			return channel(color.r) | (channel(color.g) << 8) | (channel(color.b) << 16) | (channel(color.a * alphaScale) << 24);
		}

		// Two unit vectors that are perpendicular to a unit vector and to each other.
		void basis(const glm::vec3& normal, glm::vec3& u, glm::vec3& v)
		{
			const glm::vec3 helper = std::abs(normal.y) < 0.9f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
			u = glm::normalize(glm::cross(helper, normal));
			v = glm::cross(normal, u);
		}
	}

	void DebugDraw::set_enabled(bool enabled)
	{
		m_Enabled = enabled;
		if (!enabled)
		{
			m_Frame = DebugDrawData{};
			m_TimedLines.clear();
			m_TimedTriangles.clear();
			m_LineCount = 0;
			m_TriangleCount = 0;
			m_Dropped = 0;
		}
	}

	void DebugDraw::add_line(u32 layer, const DebugLine& line, float duration)
	{
		if (m_LineCount >= MAX_LINES)
		{
			m_Dropped++;
			return;
		}

		m_LineCount++;
		if (duration > 0.0f)
		{
			m_TimedLines.push_back({ layer, line, duration });
		}
		else
		{
			m_Frame.Lines[layer].push_back(line);
		}
	}

	void DebugDraw::add_triangle(u32 layer, const DebugTriangle& triangle, float duration)
	{
		if (m_TriangleCount >= MAX_TRIANGLES)
		{
			m_Dropped++;
			return;
		}

		m_TriangleCount++;
		if (duration > 0.0f)
		{
			m_TimedTriangles.push_back({ layer, triangle, duration });
		}
		else
		{
			m_Frame.Triangles[layer].push_back(triangle);
		}
	}

	void DebugDraw::emit_line(glm::vec3 a, glm::vec3 b, glm::vec4 color, DebugDepth depth, float widthPx, float duration)
	{
		if (!finite(a) || !finite(b) || !finite(color))
		{
			return;
		}

		DebugLine line;
		line.A = a;
		line.B = b;
		line.Width = finite(widthPx) ? std::clamp(widthPx, 0.5f, 16.0f) : 1.0f;
		const float lifetime = finite(duration) ? std::max(duration, 0.0f) : 0.0f;

		line.Color = pack(color);
		switch (depth)
		{
		case DebugDepth::Tested:
			add_line(0, line, lifetime);
			break;
		case DebugDepth::AlwaysOnTop:
			add_line(1, line, lifetime);
			break;
		case DebugDepth::Faded:
			add_line(0, line, lifetime);
			line.Color = pack(color, FADED_ALPHA);
			add_line(1, line, lifetime);
			break;
		}
	}

	void DebugDraw::emit_circle(glm::vec3 center, glm::vec3 axisU, glm::vec3 axisV, float radius, glm::vec4 color, DebugDepth depth, float widthPx, float duration)
	{
		glm::vec3 previous = center + axisU * radius;
		for (u32 i = 1; i <= CIRCLE_SEGMENTS; i++)
		{
			const float angle = 2.0f * PI * static_cast<float>(i) / static_cast<float>(CIRCLE_SEGMENTS);
			const glm::vec3 next = center + (axisU * std::cos(angle) + axisV * std::sin(angle)) * radius;
			emit_line(previous, next, color, depth, widthPx, duration);
			previous = next;
		}
	}

	void DebugDraw::line(glm::vec3 a, glm::vec3 b, glm::vec4 color, DebugDepth depth, float widthPx, float duration)
	{
		if (!m_Enabled)
		{
			return;
		}
		emit_line(a, b, color, depth, widthPx, duration);
	}

	void DebugDraw::box(glm::vec3 min, glm::vec3 max, glm::vec4 color, DebugDepth depth, float widthPx, float duration)
	{
		if (!m_Enabled || !finite(min) || !finite(max) || glm::any(glm::greaterThan(min, max)))
		{
			return;
		}
		const glm::vec3 center = (min + max) * 0.5f;
		box(glm::translate(glm::mat4(1.0f), center), (max - min) * 0.5f, color, depth, widthPx, duration);
	}

	void DebugDraw::box(const glm::mat4& world, glm::vec3 halfExtents, glm::vec4 color, DebugDepth depth, float widthPx, float duration)
	{
		if (!m_Enabled || !finite(world) || !finite(halfExtents) || glm::any(glm::lessThan(halfExtents, glm::vec3(0.0f))))
		{
			return;
		}

		glm::vec3 corners[8];
		for (u32 i = 0; i < 8; i++)
		{
			const glm::vec3 sign((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f);
			corners[i] = glm::vec3(world * glm::vec4(sign * halfExtents, 1.0f));
		}
		for (u32 i = 0; i < 8; i++)
		{
			for (u32 bit = 1; bit <= 4; bit <<= 1)
			{
				if ((i & bit) == 0)
				{
					emit_line(corners[i], corners[i | bit], color, depth, widthPx, duration);
				}
			}
		}
	}

	void DebugDraw::circle(glm::vec3 center, glm::vec3 normal, float radius, glm::vec4 color, DebugDepth depth, float widthPx, float duration)
	{
		if (!m_Enabled || !finite(center) || !finite(normal) || !positive(radius) || glm::dot(normal, normal) < 1e-12f)
		{
			return;
		}
		glm::vec3 u, v;
		basis(glm::normalize(normal), u, v);
		emit_circle(center, u, v, radius, color, depth, widthPx, duration);
	}

	void DebugDraw::sphere(glm::vec3 center, float radius, glm::vec4 color, DebugDepth depth, float widthPx, float duration)
	{
		if (!m_Enabled || !finite(center) || !positive(radius))
		{
			return;
		}
		emit_circle(center, glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), radius, color, depth, widthPx, duration);
		emit_circle(center, glm::vec3(0, 1, 0), glm::vec3(0, 0, 1), radius, color, depth, widthPx, duration);
		emit_circle(center, glm::vec3(0, 0, 1), glm::vec3(1, 0, 0), radius, color, depth, widthPx, duration);
	}

	void DebugDraw::cone(glm::vec3 apex, glm::vec3 direction, float length, float angleRadians, glm::vec4 color, DebugDepth depth, float widthPx, float duration)
	{
		if (!m_Enabled || !finite(apex) || !finite(direction) || !positive(length) || !finite(angleRadians) || angleRadians <= 0.0f || angleRadians >= PI * 0.5f
			|| glm::dot(direction, direction) < 1e-12f)
		{
			return;
		}

		const glm::vec3 axis = glm::normalize(direction);
		glm::vec3 u, v;
		basis(axis, u, v);
		const glm::vec3 baseCenter = apex + axis * length;
		const float radius = length * std::tan(angleRadians);
		emit_circle(baseCenter, u, v, radius, color, depth, widthPx, duration);
		for (u32 i = 0; i < 8; i++)
		{
			const float angle = 2.0f * PI * static_cast<float>(i) / 8.0f;
			emit_line(apex, baseCenter + (u * std::cos(angle) + v * std::sin(angle)) * radius, color, depth, widthPx, duration);
		}
	}

	void DebugDraw::capsule(glm::vec3 a, glm::vec3 b, float radius, glm::vec4 color, DebugDepth depth, float widthPx, float duration)
	{
		if (!m_Enabled || !finite(a) || !finite(b) || !positive(radius))
		{
			return;
		}

		const glm::vec3 span = b - a;
		const float length = glm::length(span);
		const glm::vec3 axis = length > 1e-6f ? span / length : glm::vec3(0.0f, 1.0f, 0.0f);
		glm::vec3 u, v;
		basis(axis, u, v);

		emit_circle(a, u, v, radius, color, depth, widthPx, duration);
		emit_circle(b, u, v, radius, color, depth, widthPx, duration);
		for (const glm::vec3& side : { u, -u, v, -v })
		{
			emit_line(a + side * radius, b + side * radius, color, depth, widthPx, duration);
		}

		// The caps: a half circle in each of the two planes that hold the axis.
		constexpr u32 HALF = CIRCLE_SEGMENTS / 2;
		for (const glm::vec3& side : { u, v })
		{
			for (u32 end = 0; end < 2; end++)
			{
				const glm::vec3 center = end == 0 ? a : b;
				const float direction = end == 0 ? -1.0f : 1.0f;
				glm::vec3 previous = center + side * radius;
				for (u32 i = 1; i <= HALF; i++)
				{
					const float angle = PI * static_cast<float>(i) / static_cast<float>(HALF);
					const glm::vec3 next = center + (side * std::cos(angle) + axis * direction * std::sin(angle)) * radius;
					emit_line(previous, next, color, depth, widthPx, duration);
					previous = next;
				}
			}
		}
	}

	void DebugDraw::frustum(const glm::mat4& viewProjection, glm::vec4 color, DebugDepth depth, float widthPx, float duration)
	{
		if (!m_Enabled || !finite(viewProjection))
		{
			return;
		}
		const float determinant = glm::determinant(viewProjection);
		if (!finite(determinant) || std::abs(determinant) < 1e-20f)
		{
			return;
		}

		const glm::mat4 inverse = glm::inverse(viewProjection);
		glm::vec3 corners[8];
		for (u32 i = 0; i < 8; i++)
		{
			const glm::vec4 ndc((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : 0.0f, 1.0f);
			const glm::vec4 world = inverse * ndc;
			if (!finite(world) || std::abs(world.w) < 1e-12f)
			{
				return;
			}
			corners[i] = glm::vec3(world) / world.w;
		}
		for (u32 i = 0; i < 8; i++)
		{
			for (u32 bit = 1; bit <= 4; bit <<= 1)
			{
				if ((i & bit) == 0)
				{
					emit_line(corners[i], corners[i | bit], color, depth, widthPx, duration);
				}
			}
		}
	}

	void DebugDraw::arrow(glm::vec3 from, glm::vec3 to, glm::vec4 color, DebugDepth depth, float widthPx, float duration)
	{
		if (!m_Enabled || !finite(from) || !finite(to))
		{
			return;
		}
		const glm::vec3 span = to - from;
		const float length = glm::length(span);
		if (!positive(length))
		{
			return;
		}

		const glm::vec3 axis = span / length;
		glm::vec3 u, v;
		basis(axis, u, v);
		const float headLength = length * 0.2f;
		const float headRadius = headLength * 0.4f;
		const glm::vec3 headBase = to - axis * headLength;

		emit_line(from, to, color, depth, widthPx, duration);
		for (const glm::vec3& side : { u, -u, v, -v })
		{
			emit_line(to, headBase + side * headRadius, color, depth, widthPx, duration);
		}
	}

	void DebugDraw::grid(glm::vec3 center, glm::vec3 normal, float cell, u32 halfCount, glm::vec4 color, DebugDepth depth, float widthPx, float duration)
	{
		if (!m_Enabled || !finite(center) || !finite(normal) || !positive(cell) || glm::dot(normal, normal) < 1e-12f || halfCount > 1024)
		{
			return;
		}

		glm::vec3 u, v;
		basis(glm::normalize(normal), u, v);
		const float extent = cell * static_cast<float>(halfCount);
		for (i32 i = -static_cast<i32>(halfCount); i <= static_cast<i32>(halfCount); i++)
		{
			const float offset = cell * static_cast<float>(i);
			emit_line(center + u * offset - v * extent, center + u * offset + v * extent, color, depth, widthPx, duration);
			emit_line(center + v * offset - u * extent, center + v * offset + u * extent, color, depth, widthPx, duration);
		}
	}

	void DebugDraw::axes(const glm::mat4& world, float size, DebugDepth depth, float widthPx, float duration)
	{
		if (!m_Enabled || !finite(world) || !positive(size))
		{
			return;
		}
		const glm::vec3 origin(world[3]);
		emit_line(origin, origin + glm::vec3(world[0]) * size, glm::vec4(1.0f, 0.2f, 0.2f, 1.0f), depth, widthPx, duration);
		emit_line(origin, origin + glm::vec3(world[1]) * size, glm::vec4(0.2f, 1.0f, 0.2f, 1.0f), depth, widthPx, duration);
		emit_line(origin, origin + glm::vec3(world[2]) * size, glm::vec4(0.3f, 0.5f, 1.0f, 1.0f), depth, widthPx, duration);
	}

	void DebugDraw::triangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec4 color, DebugDepth depth, float duration)
	{
		if (!m_Enabled || !finite(a) || !finite(b) || !finite(c) || !finite(color))
		{
			return;
		}

		DebugTriangle triangle;
		triangle.P0 = a;
		triangle.P1 = b;
		triangle.P2 = c;
		const float lifetime = finite(duration) ? std::max(duration, 0.0f) : 0.0f;

		triangle.Color = pack(color);
		switch (depth)
		{
		case DebugDepth::Tested:
			add_triangle(0, triangle, lifetime);
			break;
		case DebugDepth::AlwaysOnTop:
			add_triangle(1, triangle, lifetime);
			break;
		case DebugDepth::Faded:
			add_triangle(0, triangle, lifetime);
			triangle.Color = pack(color, FADED_ALPHA);
			add_triangle(1, triangle, lifetime);
			break;
		}
	}

	void DebugDraw::quad(glm::vec3 center, glm::vec3 right, glm::vec3 up, glm::vec4 color, DebugDepth depth, float duration)
	{
		if (!m_Enabled || !finite(center) || !finite(right) || !finite(up))
		{
			return;
		}
		const glm::vec3 p0 = center - right - up;
		const glm::vec3 p1 = center + right - up;
		const glm::vec3 p2 = center + right + up;
		const glm::vec3 p3 = center - right + up;
		triangle(p0, p1, p2, color, depth, duration);
		triangle(p0, p2, p3, color, depth, duration);
	}

	void DebugDraw::pick_icon(EntityGuid guid, glm::vec3 position, float radiusPx)
	{
		if (!m_Enabled || !finite(position) || !positive(radiusPx))
		{
			return;
		}
		if (m_Frame.PickIcons.size() >= MAX_PICK_ICONS)
		{
			m_Dropped++;
			return;
		}
		m_Frame.PickIcons.push_back({ guid, position, radiusPx });
	}

	DebugDrawData DebugDraw::seal(float deltaTime)
	{
		DebugDrawData out = std::move(m_Frame);
		m_Frame = DebugDrawData{};

		for (const TimedLine& timed : m_TimedLines)
		{
			out.Lines[timed.Layer].push_back(timed.Line);
		}
		for (const TimedTriangle& timed : m_TimedTriangles)
		{
			out.Triangles[timed.Layer].push_back(timed.Triangle);
		}

		const float step = finite(deltaTime) ? std::max(deltaTime, 0.0f) : 0.0f;
		for (TimedLine& timed : m_TimedLines)
		{
			timed.Remaining -= step;
		}
		for (TimedTriangle& timed : m_TimedTriangles)
		{
			timed.Remaining -= step;
		}
		std::erase_if(m_TimedLines, [](const TimedLine& timed) { return timed.Remaining <= 0.0f; });
		std::erase_if(m_TimedTriangles, [](const TimedTriangle& timed) { return timed.Remaining <= 0.0f; });

		m_LineCount = static_cast<u32>(m_TimedLines.size());
		m_TriangleCount = static_cast<u32>(m_TimedTriangles.size());

		m_DroppedLastSeal = m_Dropped;
		if (m_Dropped > 0)
		{
			WI_LOG(Warning, "Renderer", "Debug draw dropped {0} shapes over the per-frame caps", m_Dropped);
			m_Dropped = 0;
		}
		return out;
	}
}
