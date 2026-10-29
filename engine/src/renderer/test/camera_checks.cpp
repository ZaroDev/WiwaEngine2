#include <pch.h>
#include "render_test.h"

#include <limits>
#include <numbers>

#include "../scene/editor_camera.h"
#include "../scene/scene_extractor.h"

// Checks of the camera construction and the editor camera. Plain logic, no GPU.
namespace wiwa
{
	namespace
	{
		bool all_finite(const glm::mat4& matrix)
		{
			for (int column = 0; column < 4; column++)
			{
				for (int row = 0; row < 4; row++)
				{
					if (!std::isfinite(matrix[column][row]))
					{
						return false;
					}
				}
			}
			return true;
		}

		glm::vec3 project(const RenderCamera& camera, glm::vec3 point)
		{
			const glm::vec4 clip = camera.Projection * camera.View * glm::vec4(point, 1.0f);
			return glm::vec3(clip) / clip.w;
		}
	}

	void register_camera_checks()
	{
		register_render_check("camera_matrices", [](RenderCheckContext& context)
			{
				Camera camera;
				camera.FovY = 60.0f;
				camera.Near = 0.1f;
				camera.Far = 100.0f;
				const glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 5.0f));
				const RenderCamera result = make_render_camera(camera, world, 2.0f);

				const glm::vec3 center = project(result, glm::vec3(0.0f));
				context.check(std::abs(center.x) < 1e-5f && std::abs(center.y) < 1e-5f, "the point it looks at is in the middle");
				context.check(center.z > 0.0f && center.z < 1.0f, "its depth is in 0..1");

				// At distance 5 the view is 5 * tan(30 degrees) high, twice that times the aspect wide.
				const float halfHeight = 5.0f * std::tan(glm::radians(30.0f));
				const glm::vec3 edge = project(result, glm::vec3(halfHeight * 2.0f, 0.0f, 0.0f));
				context.check(std::abs(edge.x - 1.0f) < 1e-4f, "a point at the side edge has x = 1");
				const glm::vec3 top = project(result, glm::vec3(0.0f, halfHeight, 0.0f));
				context.check(std::abs(top.y - 1.0f) < 1e-4f, "a point at the top edge has y = 1");

				context.check(std::abs(project(result, glm::vec3(0.0f, 0.0f, 5.0f - 0.1f)).z) < 1e-4f, "the near plane is depth 0");
				context.check(std::abs(project(result, glm::vec3(0.0f, 0.0f, 5.0f - 100.0f)).z - 1.0f) < 1e-4f, "the far plane is depth 1");
				context.check(result.Position == glm::vec3(0.0f, 0.0f, 5.0f), "the position");
			});

		register_render_check("camera_degenerate", [](RenderCheckContext& context)
			{
				const float nan = std::numeric_limits<float>::quiet_NaN();
				bool degenerate = false;

				Camera flat;
				flat.Near = 1.0f;
				flat.Far = 1.0f;
				RenderCamera result = make_render_camera(flat, glm::mat4(1.0f), 1.0f, &degenerate);
				context.check(degenerate && result.Near < result.Far && all_finite(result.Projection), "near == far gives a usable camera");

				result = make_render_camera(Camera{}, glm::mat4(1.0f), 0.0f, &degenerate);
				context.check(degenerate && all_finite(result.Projection), "aspect 0");

				glm::mat4 broken(1.0f);
				broken[3][1] = nan;
				result = make_render_camera(Camera{}, broken, 1.0f, &degenerate);
				context.check(degenerate && all_finite(result.View) && all_finite(result.Projection) && std::isfinite(result.Position.y), "a NaN in the transform");

				result = make_render_camera(Camera{}, glm::mat4(0.0f), 1.0f, &degenerate);
				context.check(degenerate && all_finite(result.View), "a transform that cannot be inverted");

				Camera wide;
				wide.FovY = 400.0f;
				wide.Near = nan;
				result = make_render_camera(wide, glm::mat4(1.0f), 1.0f, &degenerate);
				context.check(degenerate && all_finite(result.Projection), "a wild field of view and a NaN near plane");

				result = make_render_camera(Camera{}, glm::mat4(1.0f), 1.5f, &degenerate);
				context.check(!degenerate, "a good camera is left alone");
			});

		register_render_check("editor_camera_moves", [](RenderCheckContext& context)
			{
				EditorCamera camera;
				const glm::vec3 start = camera.position();
				const glm::vec3 front = camera.forward();
				EditorCameraInput forward;
				forward.Move = glm::vec3(0.0f, 0.0f, 1.0f);
				camera.apply(1.0f, forward);
				const glm::vec3 moved = camera.position() - start;
				context.check(glm::length(moved - front * camera.speed()) < 1e-4f && std::abs(camera.speed() - 4.0f) < 1e-5f, "one second forward at speed 4 moves 4 units along its forward");

				EditorCameraInput fast = forward;
				fast.Fast = true;
				const glm::vec3 before = camera.position();
				camera.apply(1.0f, fast);
				context.check(glm::length(camera.position() - before) > 11.0f, "shift is faster");

				EditorCameraInput up;
				up.Move = glm::vec3(0.0f, 1.0f, 0.0f);
				const float height = camera.position().y;
				camera.apply(0.5f, up);
				context.check(std::abs(camera.position().y - (height + 2.0f)) < 1e-4f, "up is world up");

				EditorCameraInput look;
				look.Look = glm::vec2(0.0f, -100000.0f);
				camera.apply(0.0f, look);
				context.check(camera.pitch_degrees() <= 89.0001f && camera.pitch_degrees() > 88.0f, "pitch up is limited to 89 degrees");
				look.Look = glm::vec2(0.0f, 200000.0f);
				camera.apply(0.0f, look);
				context.check(camera.pitch_degrees() >= -89.0001f && camera.pitch_degrees() < -88.0f, "pitch down too");

				EditorCameraInput scroll;
				scroll.Scroll = 100.0f;
				camera.apply(0.0f, scroll);
				context.check(camera.speed() <= 1000.0f, "the speed has a ceiling");

				EditorCamera yawed;
				EditorCameraInput turn;
				turn.Look = glm::vec2(std::numbers::pi_v<float> / 2.0f / 0.0035f, 0.0f);
				yawed.apply(0.0f, turn);
				context.check(yawed.forward().x > 0.9f * std::cos(glm::radians(15.0f)), "moving the mouse left-to-right turns the view to the right (+X)");

				const RenderCamera result = yawed.get(1.5f);
				context.check(all_finite(result.View) && all_finite(result.Projection), "the camera it makes is finite");
			});
	}
}
