#include <pch.h>
#include "render_test.h"

#include <limits>

#include "../debug/debug_draw.h"

// Checks of the DebugDraw collector: where shapes land, how long they live, the cap and bad input. Plain logic, no GPU.
namespace wiwa
{
	namespace
	{
		constexpr glm::vec4 WHITE(1.0f);

		u32 alpha_of(u32 packed) { return packed >> 24; }
		size_t line_count(const DebugDrawData& data) { return data.Lines[0].size() + data.Lines[1].size(); }
		size_t triangle_count(const DebugDrawData& data) { return data.Triangles[0].size() + data.Triangles[1].size(); }
	}

	void register_debug_draw_checks()
	{
		register_render_check("debug_draw_line", [](RenderCheckContext& context)
			{
				DebugDraw draw;
				draw.line(glm::vec3(0.0f), glm::vec3(1.0f, 2.0f, 3.0f), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), DebugDepth::Tested, 3.0f);
				draw.line(glm::vec3(0.0f), glm::vec3(1.0f), WHITE, DebugDepth::AlwaysOnTop);
				draw.line(glm::vec3(0.0f), glm::vec3(1.0f), WHITE, DebugDepth::Faded);
				const DebugDrawData data = draw.seal(0.016f);

				context.check(data.Lines[0].size() == 2, "tested and the tested half of faded land in the tested list");
				context.check(data.Lines[1].size() == 2, "always-on-top and the faded copy land in the top list");
				context.check(data.Lines[0][0].Color == 0xFF0000FFu, "red packs as RGBA8 with R in the low byte");
				context.check(data.Lines[0][0].Width == 3.0f, "the width is kept");
				context.check(data.Lines[0][0].B == glm::vec3(1.0f, 2.0f, 3.0f), "the end point is kept");
				context.check(alpha_of(data.Lines[1][0].Color) == 255, "always-on-top keeps its alpha");
				// the faded copy is the last one in the top list
				const u32 faded = alpha_of(data.Lines[1][1].Color);
				context.check(faded >= 62 && faded <= 65, "the faded copy has a quarter of the alpha");
				context.check(draw.seal(0.016f).empty(), "an immediate shape is in exactly one seal");
			});

		register_render_check("debug_draw_shapes", [](RenderCheckContext& context)
			{
				DebugDraw draw;
				draw.sphere(glm::vec3(0.0f), 1.0f, WHITE);
				context.check(line_count(draw.seal(0.0f)) == 3 * 32, "a sphere is three circles of 32 segments");

				draw.box(glm::vec3(-1.0f), glm::vec3(1.0f), WHITE);
				context.check(line_count(draw.seal(0.0f)) == 12, "a box has 12 edges");

				draw.box(glm::mat4(1.0f), glm::vec3(1.0f), WHITE);
				context.check(line_count(draw.seal(0.0f)) == 12, "an oriented box has 12 edges");

				draw.frustum(glm::perspectiveRH_ZO(1.0f, 1.0f, 0.1f, 10.0f) * glm::lookAt(glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f)), WHITE);
				context.check(line_count(draw.seal(0.0f)) == 12, "a frustum has 12 edges");

				draw.quad(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), WHITE);
				context.check(triangle_count(draw.seal(0.0f)) == 2, "a quad is two triangles");

				draw.triangle(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), WHITE);
				context.check(triangle_count(draw.seal(0.0f)) == 1, "a triangle is one triangle");

				draw.circle(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 1.0f, WHITE);
				context.check(line_count(draw.seal(0.0f)) == 32, "a circle has 32 segments");

				draw.arrow(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 2.0f), WHITE);
				context.check(line_count(draw.seal(0.0f)) >= 5, "an arrow has a shaft and a head");

				draw.axes(glm::mat4(1.0f), 1.0f);
				context.check(line_count(draw.seal(0.0f)) == 3, "axes are three lines");

				draw.grid(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 1.0f, 2);
				context.check(line_count(draw.seal(0.0f)) == 2 * (2 * 2 + 1), "a grid of half count 2 has 5 lines each way");

				draw.cone(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), 2.0f, 0.5f, WHITE);
				context.check(line_count(draw.seal(0.0f)) > 32, "a cone has a base circle and side lines");

				draw.capsule(glm::vec3(0.0f), glm::vec3(0.0f, 2.0f, 0.0f), 0.5f, WHITE);
				context.check(line_count(draw.seal(0.0f)) > 0, "a capsule draws lines");
			});

		register_render_check("debug_draw_duration", [](RenderCheckContext& context)
			{
				DebugDraw draw;
				draw.line(glm::vec3(0.0f), glm::vec3(1.0f), WHITE, DebugDepth::Tested, 1.0f, 0.5f);
				draw.line(glm::vec3(0.0f), glm::vec3(2.0f), WHITE);

				context.check(line_count(draw.seal(0.2f)) == 2, "first seal: the timed and the immediate shape");
				context.check(line_count(draw.seal(0.2f)) == 1, "second seal: only the timed shape (0.3 s left before this seal ages it)");
				context.check(line_count(draw.seal(0.2f)) == 1, "third seal: the timed shape (0.1 s left before aging)");
				context.check(draw.seal(0.2f).empty(), "fourth seal: the timed shape has expired");
			});

		register_render_check("debug_draw_cap", [](RenderCheckContext& context)
			{
				DebugDraw draw;
				for (u32 i = 0; i < DebugDraw::MAX_LINES + 10; i++)
				{
					draw.line(glm::vec3(0.0f), glm::vec3(1.0f), WHITE);
				}
				const DebugDrawData data = draw.seal(0.0f);
				context.check(line_count(data) == DebugDraw::MAX_LINES, "lines are capped");
				context.check(draw.dropped_last_seal() == 10, "the dropped lines are counted");

				draw.line(glm::vec3(0.0f), glm::vec3(1.0f), WHITE);
				draw.seal(0.0f);
				context.check(draw.dropped_last_seal() == 0, "the count is per seal");
			});

		register_render_check("debug_draw_invalid", [](RenderCheckContext& context)
			{
				const float nan = std::numeric_limits<float>::quiet_NaN();
				const float inf = std::numeric_limits<float>::infinity();
				DebugDraw draw;
				draw.line(glm::vec3(nan, 0.0f, 0.0f), glm::vec3(1.0f), WHITE);
				draw.line(glm::vec3(0.0f), glm::vec3(inf, 0.0f, 0.0f), WHITE);
				draw.line(glm::vec3(0.0f), glm::vec3(1.0f), glm::vec4(nan, 0.0f, 0.0f, 1.0f));
				draw.sphere(glm::vec3(0.0f), 0.0f, WHITE);
				draw.sphere(glm::vec3(0.0f), -1.0f, WHITE);
				draw.sphere(glm::vec3(nan), 1.0f, WHITE);
				draw.circle(glm::vec3(0.0f), glm::vec3(0.0f), 1.0f, WHITE);
				draw.cone(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f), -1.0f, 0.5f, WHITE);
				draw.capsule(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), -0.5f, WHITE);
				draw.grid(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 0.0f, 3);
				draw.frustum(glm::mat4(0.0f), WHITE);
				draw.frustum(glm::mat4(nan), WHITE);
				draw.box(glm::vec3(nan), glm::vec3(1.0f), WHITE);
				draw.box(glm::mat4(1.0f), glm::vec3(-1.0f), WHITE);
				draw.triangle(glm::vec3(nan), glm::vec3(1.0f), glm::vec3(2.0f), WHITE);
				draw.pick_icon(EntityGuid{ 1, 2 }, glm::vec3(nan), 10.0f);
				draw.pick_icon(EntityGuid{ 1, 2 }, glm::vec3(0.0f), -1.0f);
				const DebugDrawData data = draw.seal(0.0f);
				context.check(data.empty(), "bad input draws nothing");
			});

		register_render_check("debug_draw_disabled", [](RenderCheckContext& context)
			{
				DebugDraw draw;
				context.check(draw.enabled(), "enabled by default");
				draw.set_enabled(false);
				for (int i = 0; i < 1000; i++)
				{
					draw.line(glm::vec3(0.0f), glm::vec3(1.0f), WHITE, DebugDepth::Tested, 1.0f, 10.0f);
					draw.sphere(glm::vec3(0.0f), 1.0f, WHITE);
					draw.pick_icon(EntityGuid{ 1, 2 }, glm::vec3(0.0f), 10.0f);
				}
				context.check(draw.seal(0.016f).empty(), "nothing is stored while disabled");

				draw.set_enabled(true);
				draw.line(glm::vec3(0.0f), glm::vec3(1.0f), WHITE, DebugDepth::Tested, 1.0f, 10.0f);
				draw.set_enabled(false);
				context.check(draw.seal(0.016f).empty(), "disabling drops the timed shapes too");
				draw.set_enabled(true);
				context.check(draw.seal(0.016f).empty(), "and they do not come back");
			});

		register_render_check("debug_draw_pick_icons", [](RenderCheckContext& context)
			{
				DebugDraw draw;
				draw.pick_icon(EntityGuid{ 3, 4 }, glm::vec3(1.0f, 2.0f, 3.0f), 12.0f);
				const DebugDrawData data = draw.seal(0.0f);
				context.check(data.PickIcons.size() == 1 && data.PickIcons[0].Guid == EntityGuid({ 3, 4 }) && data.PickIcons[0].RadiusPx == 12.0f, "a pick icon is kept");
				context.check(line_count(data) == 0 && triangle_count(data) == 0, "a pick icon draws nothing");
				context.check(draw.seal(0.0f).PickIcons.empty(), "pick icons are per frame");
			});
	}
}
