#include <pch.h>

#include "../passes/taa_math.h"
#include "scene_tester.h"

// Checks of the temporal chain: the TAA maths, the velocity target, TAA and motion blur.
namespace wiwa
{
	namespace
	{
		// Where a point lands in an image of size x size pixels (origin top left), fractions included.
		glm::vec2 project_pixel(const glm::mat4& projection, const glm::mat4& view, glm::vec3 point, u32 size)
		{
			const glm::vec4 clip = projection * view * glm::vec4(point, 1.0f);
			const glm::vec2 ndc = glm::vec2(clip) / clip.w;
			return glm::vec2((ndc.x * 0.5f + 0.5f) * static_cast<float>(size), (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(size));
		}
	}

	void register_temporal_checks()
	{
		register_render_check("taa_halton_sequence", [](RenderCheckContext& context)
			{
				context.check(halton(1, 2) == 0.5f && halton(2, 2) == 0.25f && halton(3, 2) == 0.75f, "base 2 radical inverse");
				context.check(std::abs(halton(1, 3) - 1.0f / 3.0f) < 1e-6f && std::abs(halton(2, 3) - 2.0f / 3.0f) < 1e-6f, "base 3 radical inverse");

				bool inside = true;
				bool distinct = true;
				for (u64 i = 0; i < 8; i++)
				{
					const glm::vec2 jitter = taa_jitter(i);
					inside = inside && std::abs(jitter.x) <= 0.5f && std::abs(jitter.y) <= 0.5f;
					for (u64 j = i + 1; j < 8; j++)
					{
						distinct = distinct && taa_jitter(j) != jitter;
					}
				}
				context.check(inside, "every jitter is inside the pixel");
				context.check(distinct, "the eight jitters differ");
				context.check(taa_jitter(8) == taa_jitter(0) && taa_jitter(13) == taa_jitter(5), "the sequence repeats after eight frames");
			});

		register_render_check("taa_jitter_projects_to_offset", [](RenderCheckContext& context)
			{
				constexpr u32 SIZE = 64;
				const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
				const glm::mat4 perspective = glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 100.0f);
				const glm::mat4 orthographic = glm::orthoRH_ZO(-4.0f, 4.0f, -4.0f, 4.0f, 0.1f, 100.0f);
				const glm::vec2 jitter(0.5f, -0.25f);

				for (const glm::mat4& projection : { perspective, orthographic })
				{
					const glm::mat4 jittered = apply_jitter(projection, jitter, SIZE, SIZE);
					for (const float z : { 1.0f, -26.0f })
					{
						const glm::vec3 point(0.3f, -0.2f, z);
						const glm::vec2 offset = project_pixel(jittered, view, point, SIZE) - project_pixel(projection, view, point, SIZE);
						context.check(std::abs(offset.x - jitter.x) < 1e-3f && std::abs(offset.y - jitter.y) < 1e-3f, "a jittered projection moves a point by the jitter (z " + std::to_string(z) + ", " + (projection[3][3] == 1.0f ? "orthographic" : "perspective") + ")");
					}
				}
			});

		register_render_check("taa_clip_and_blend", [](RenderCheckContext& context)
			{
				glm::vec3 neighbourhood[9];
				for (int i = 0; i < 9; i++)
				{
					neighbourhood[i] = glm::vec3(0.4f + 0.02f * static_cast<float>(i), 0.1f, 0.0f);
				}
				const ColorBox box = taa_variance_box(neighbourhood, 1.25f);
				const glm::vec3 inside = (box.Min + box.Max) * 0.5f;
				context.check(taa_clip_to_box(inside, box) == inside, "a history inside the box is kept");

				const glm::vec3 outside(box.Max.x + 1.0f, inside.y, inside.z);
				const glm::vec3 clipped = taa_clip_to_box(outside, box);
				context.check(std::abs(clipped.x - box.Max.x) < 1e-4f, "a history outside the box is moved onto its surface");

				const glm::vec3 current(0.2f, 0.4f, 0.6f);
				const glm::vec3 history(0.6f, 0.4f, 0.2f);
				const glm::vec3 none = taa_blend(current, history, 0.0f);
				const glm::vec3 all = taa_blend(current, history, 1.0f);
				context.check(glm::distance(none, current) < 1e-5f && glm::distance(all, history) < 1e-5f, "feedback 0 gives the new color, 1 the history");

				const glm::vec3 mixed = taa_blend(glm::vec3(100.0f), glm::vec3(0.1f), 0.9f);
				context.check(mixed.x < 5.0f, "a very bright new pixel cannot swamp the history");
				context.check(std::abs(taa_luma(glm::vec3(1.0f)) - 1.0f) < 1e-5f && glm::distance(ycocg_to_rgb(rgb_to_ycocg(current)), current) < 1e-5f, "luma and the YCoCg round trip");
			});

		register_render_check("taa_feedback_falls_with_motion", [](RenderCheckContext& context)
			{
				context.check(std::abs(taa_feedback(0.9f, 0.0f, 0.0f) - 0.9f) < 1e-6f, "no motion keeps the feedback");
				context.check(std::abs(taa_feedback(0.9f, 0.0f, 16.0f) - 0.45f) < 1e-6f, "16 pixels per frame halves it");
				context.check(std::abs(taa_feedback(0.9f, 1.0f, 0.0f) - 0.45f) < 1e-6f, "a history clipped all the way halves it");
				context.check(std::abs(taa_feedback(0.9f, 1.0f, 16.0f) - 0.225f) < 1e-6f, "both quarter it");
				context.check(std::abs(taa_feedback(0.9f, 5.0f, 100.0f) - 0.225f) < 1e-6f, "the reductions are capped");
			});
	}
}
