#pragma once

#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include <math/math.h>
#include <scene/entity_guid.h>

namespace wiwa
{
	enum class DebugDepth : u8
	{
		Tested,      // hidden behind scene geometry
		AlwaysOnTop, // drawn over everything
		Faded        // always drawn, a quarter as strong where scene geometry hides it
	};

	// One line as the debug shader reads it (DebugLine in debug.hlsl). Color is RGBA8, red in the low byte.
	struct DebugLine
	{
		glm::vec3 A{ 0.0f };
		float Width = 1.0f; // pixels
		glm::vec3 B{ 0.0f };
		u32 Color = 0xFFFFFFFFu;
	};
	static_assert(sizeof(DebugLine) == 32);

	// One filled triangle as the debug shader reads it (DebugTriangle in debug.hlsl).
	struct DebugTriangle
	{
		glm::vec3 P0{ 0.0f };
		u32 Color = 0xFFFFFFFFu;
		glm::vec3 P1{ 0.0f };
		u32 Pad0 = 0;
		glm::vec3 P2{ 0.0f };
		u32 Pad1 = 0;
	};
	static_assert(sizeof(DebugTriangle) == 48);

	// Something a click can hit that is not a mesh: the picking pass draws a camera-facing quad of RadiusPx for it.
	struct DebugPickIcon
	{
		EntityGuid Guid{};
		glm::vec3 Position{ 0.0f };
		float RadiusPx = 8.0f;
	};

	// What one frame of debug drawing is: plain data, carried by the render batch. Index 0 is depth tested, index 1 always on top.
	struct DebugDrawData
	{
		std::vector<DebugLine> Lines[2]{};
		std::vector<DebugTriangle> Triangles[2]{};
		std::vector<DebugPickIcon> PickIcons{};

		bool empty() const
		{
			return Lines[0].empty() && Lines[1].empty() && Triangles[0].empty() && Triangles[1].empty() && PickIcons.empty();
		}
	};

	// Immediate-mode shapes, collected on the main thread and handed to the render thread once per frame (seal). Anything that is
	// not finite, or has a size that is not positive, is dropped. Not thread safe.
	class DebugDraw
	{
	public:
		static constexpr u32 MAX_LINES = 1u << 18;
		static constexpr u32 MAX_TRIANGLES = 1u << 16;
		static constexpr u32 MAX_PICK_ICONS = 1u << 16;

		// Disabled: every call returns at once and nothing is stored (what is stored is dropped).
		void set_enabled(bool enabled);
		bool enabled() const { return m_Enabled; }

		// widthPx is the line width in pixels; duration keeps the shape for that many seconds (0 = this frame only).
		void line(glm::vec3 a, glm::vec3 b, glm::vec4 color, DebugDepth depth = DebugDepth::Tested, float widthPx = 1.0f, float duration = 0.0f);
		void box(glm::vec3 min, glm::vec3 max, glm::vec4 color, DebugDepth depth = DebugDepth::Tested, float widthPx = 1.0f, float duration = 0.0f);
		void box(const glm::mat4& world, glm::vec3 halfExtents, glm::vec4 color, DebugDepth depth = DebugDepth::Tested, float widthPx = 1.0f, float duration = 0.0f);
		void sphere(glm::vec3 center, float radius, glm::vec4 color, DebugDepth depth = DebugDepth::Tested, float widthPx = 1.0f, float duration = 0.0f);
		void circle(glm::vec3 center, glm::vec3 normal, float radius, glm::vec4 color, DebugDepth depth = DebugDepth::Tested, float widthPx = 1.0f, float duration = 0.0f);
		// A cone from its apex along direction, length long, with a half angle in radians.
		void cone(glm::vec3 apex, glm::vec3 direction, float length, float angleRadians, glm::vec4 color, DebugDepth depth = DebugDepth::Tested, float widthPx = 1.0f, float duration = 0.0f);
		void capsule(glm::vec3 a, glm::vec3 b, float radius, glm::vec4 color, DebugDepth depth = DebugDepth::Tested, float widthPx = 1.0f, float duration = 0.0f);
		// The 12 edges of the volume a view-projection matrix sees (depth range 0..1).
		void frustum(const glm::mat4& viewProjection, glm::vec4 color, DebugDepth depth = DebugDepth::Tested, float widthPx = 1.0f, float duration = 0.0f);
		void arrow(glm::vec3 from, glm::vec3 to, glm::vec4 color, DebugDepth depth = DebugDepth::Tested, float widthPx = 1.0f, float duration = 0.0f);
		// halfCount cells each way from the center, in the plane with this normal.
		void grid(glm::vec3 center, glm::vec3 normal, float cell, u32 halfCount, glm::vec4 color = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f), DebugDepth depth = DebugDepth::Tested, float widthPx = 1.0f, float duration = 0.0f);
		// The transform's X, Y and Z axes in red, green and blue.
		void axes(const glm::mat4& world, float size, DebugDepth depth = DebugDepth::Tested, float widthPx = 1.0f, float duration = 0.0f);
		void triangle(glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec4 color, DebugDepth depth = DebugDepth::Tested, float duration = 0.0f);
		// right and up are half extents.
		void quad(glm::vec3 center, glm::vec3 right, glm::vec3 up, glm::vec4 color, DebugDepth depth = DebugDepth::Tested, float duration = 0.0f);
		// Pickable only, draws nothing; lasts this frame.
		void pick_icon(EntityGuid guid, glm::vec3 position, float radiusPx);

		// Moves this frame's shapes (and the timed ones that are still alive) out, then ages the timed ones by deltaTime.
		DebugDrawData seal(float deltaTime);
		// How many shapes the previous seal dropped for being over the caps.
		u32 dropped_last_seal() const { return m_DroppedLastSeal; }

	private:
		struct TimedLine
		{
			u32 Layer = 0;
			DebugLine Line{};
			float Remaining = 0.0f;
		};

		struct TimedTriangle
		{
			u32 Layer = 0;
			DebugTriangle Triangle{};
			float Remaining = 0.0f;
		};

		void add_line(u32 layer, const DebugLine& line, float duration);
		void add_triangle(u32 layer, const DebugTriangle& triangle, float duration);
		void emit_line(glm::vec3 a, glm::vec3 b, glm::vec4 color, DebugDepth depth, float widthPx, float duration);
		void emit_circle(glm::vec3 center, glm::vec3 axisU, glm::vec3 axisV, float radius, glm::vec4 color, DebugDepth depth, float widthPx, float duration);

		bool m_Enabled = true;
		DebugDrawData m_Frame{};
		std::vector<TimedLine> m_TimedLines{};
		std::vector<TimedTriangle> m_TimedTriangles{};
		u32 m_LineCount = 0;     // in m_Frame and the timed list
		u32 m_TriangleCount = 0;
		u32 m_Dropped = 0;
		u32 m_DroppedLastSeal = 0;
	};
}
