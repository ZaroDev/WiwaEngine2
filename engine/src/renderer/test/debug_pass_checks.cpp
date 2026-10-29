#include <pch.h>
#include "render_test.h"

#include "cross_backend.h"
#include "scene_tester.h"

// Checks of the debug draw pass: where lines land, the three depth modes, an empty frame and a resize. Whole frames through the renderer.
namespace wiwa
{
	namespace
	{
		constexpr u32 SIZE = 64;
		constexpr glm::vec4 WHITE(1.0f);
		const float HALF_TAN = std::tan(glm::radians(30.0f)); // the tester's camera: 60 degrees, 4 units from the origin, looking down -Z

		// The world point on the plane z = depthZ that lands in the middle of pixel (px + 0.5, py + 0.5).
		glm::vec3 at_pixel(float px, float py, float depthZ = 0.0f)
		{
			const float distance = 4.0f - depthZ;
			const float ndcX = (px + 0.5f) / SIZE * 2.0f - 1.0f;
			const float ndcY = 1.0f - (py + 0.5f) / SIZE * 2.0f;
			return glm::vec3(ndcX * distance * HALF_TAN, ndcY * distance * HALF_TAN, depthZ);
		}

		void dark_scene(SceneTester& test)
		{
			SceneSettings settings;
			settings.AmbientIntensity = 0.0f;
			settings.BackgroundColor = glm::vec3(0.0f);
			test.scene().set_settings(settings);
		}

		bool is_white(glm::ivec3 color) { return color.r >= 250 && color.g >= 250 && color.b >= 250; }

		bool same_pixel(const std::vector<u8>& a, const std::vector<u8>& b, int x, int y)
		{
			const size_t offset = (static_cast<size_t>(y) * SIZE + static_cast<size_t>(x)) * 4;
			return a[offset] == b[offset] && a[offset + 1] == b[offset + 1] && a[offset + 2] == b[offset + 2];
		}

		// How many pixels of the image differ from the baseline outside rows firstRow..lastRow.
		int changed_outside_rows(const std::vector<u8>& image, const std::vector<u8>& baseline, int firstRow, int lastRow)
		{
			int changed = 0;
			for (int y = 0; y < static_cast<int>(SIZE); y++)
			{
				for (int x = 0; x < static_cast<int>(SIZE); x++)
				{
					if ((y < firstRow || y > lastRow) && !same_pixel(image, baseline, x, y))
					{
						changed++;
					}
				}
			}
			return changed;
		}
	}

	void register_debug_pass_checks()
	{
		register_render_check("debug_pass_line_pixels", [](RenderCheckContext& context)
			{
				SceneTester test(context, SIZE);
				dark_scene(test);
				context.check(test.render(), "render the baseline");
				const std::vector<u8> baseline = test.pixels();

				const glm::vec3 from = at_pixel(8.0f, 32.0f);
				const glm::vec3 to = at_pixel(55.0f, 32.0f);
				test.debug().line(from, to, WHITE, DebugDepth::AlwaysOnTop, 1.0f, 100.0f);
				context.check(test.render(), "render with a 1 px line");
				const std::vector<u8> thin = test.pixels();
				context.check(is_white(test.pixel(10, 32)) && is_white(test.pixel(32, 32)) && is_white(test.pixel(53, 32)), "the line covers its row");
				context.check(same_pixel(thin, baseline, 32, 31) && same_pixel(thin, baseline, 32, 33), "a 1 px line covers one row");
				context.check(changed_outside_rows(thin, baseline, 32, 32) == 0, "nothing else changes");

				test.debug().set_enabled(false);
				test.debug().set_enabled(true);
				test.debug().line(from, to, WHITE, DebugDepth::AlwaysOnTop, 3.0f, 100.0f);
				context.check(test.render(), "render with a 3 px line");
				const std::vector<u8> thick = test.pixels();
				context.check(is_white(test.pixel(32, 31)) && is_white(test.pixel(32, 32)) && is_white(test.pixel(32, 33)), "a 3 px line covers three rows");
				context.check(same_pixel(thick, baseline, 32, 30) && same_pixel(thick, baseline, 32, 34), "and no more");
				context.check(changed_outside_rows(thick, baseline, 31, 33) == 0, "nothing else changes with the thick line");

				test.debug().set_enabled(false);
				test.debug().set_enabled(true);
				test.debug().line(from, to, glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), DebugDepth::AlwaysOnTop, 1.0f, 100.0f);
				context.check(test.render(), "render a red line");
				const glm::ivec3 red = test.pixel(32, 32);
				context.check(red.r >= 250 && red.g <= 5 && red.b <= 5, "the color is what was asked for");
			});

		register_render_check("debug_pass_depth_modes", [](RenderCheckContext& context)
			{
				SceneTester test(context, SIZE);
				dark_scene(test);
				test.add_instance(test.add_mesh(make_test_cube(test.add_material(test.color_material(glm::vec3(1.0f, 0.0f, 0.0f))))), glm::mat4(1.0f));
				test.add_light(test.sun(glm::vec3(0.0f, 0.0f, -1.0f)));
				context.check(test.render(), "render the cube");
				const glm::ivec3 cube = test.pixel(32, 32);
				context.check(cube.r > 20 && test.pixel(6, 32).r < 5, "the cube is lit and in the middle, with background beside it");

				auto draw = [&](DebugDepth depth, float z)
				{
					test.debug().set_enabled(false); // drops the previous shape
					test.debug().set_enabled(true);
					test.debug().line(at_pixel(2.0f, 32.0f, z), at_pixel(61.0f, 32.0f, z), WHITE, depth, 1.0f, 100.0f);
					return test.render();
				};

				context.check(draw(DebugDepth::Tested, -2.0f), "tested line behind the cube");
				context.check(std::abs(test.pixel(32, 32).r - cube.r) <= 2 && std::abs(test.pixel(32, 32).g - cube.g) <= 2, "the cube hides the tested line");
				context.check(is_white(test.pixel(6, 32)), "the tested line shows beside the cube");

				context.check(draw(DebugDepth::AlwaysOnTop, -2.0f), "always-on-top line behind the cube");
				context.check(is_white(test.pixel(32, 32)), "always-on-top shows through the cube");

				context.check(draw(DebugDepth::Faded, -2.0f), "faded line behind the cube");
				const glm::ivec3 faded = test.pixel(32, 32);
				const glm::ivec3 expected = cube + (glm::ivec3(255) - cube) / 4;
				context.check(std::abs(faded.r - expected.r) <= 4 && std::abs(faded.g - expected.g) <= 4 && std::abs(faded.b - expected.b) <= 4, "faded shows a quarter as strong behind the cube");
				context.check(is_white(test.pixel(6, 32)), "faded is full strength where nothing hides it");

				context.check(draw(DebugDepth::Faded, 2.0f), "faded line in front of the cube");
				context.check(is_white(test.pixel(32, 32)), "faded is full strength in front of the cube");
				context.check(draw(DebugDepth::Tested, 2.0f), "tested line in front of the cube");
				context.check(is_white(test.pixel(32, 32)), "tested shows in front of the cube");
			});

		register_render_check("debug_pass_empty_noop", [](RenderCheckContext& context)
			{
				SceneTester test(context, SIZE);
				dark_scene(test);
				test.add_instance(test.add_mesh(make_test_cube(test.add_material(test.color_material(glm::vec3(0.2f, 0.6f, 0.9f))))), glm::mat4(1.0f));
				context.check(test.render(), "render the baseline");
				const std::vector<u8> baseline = test.pixels();

				test.debug().line(at_pixel(8.0f, 8.0f), at_pixel(50.0f, 50.0f), WHITE, DebugDepth::AlwaysOnTop, 2.0f, 100.0f);
				context.check(test.render(), "render with a line");
				context.check(SceneTester::max_difference(baseline, test.pixels()) > 100, "the line is drawn");

				test.debug().set_enabled(false);
				context.check(test.render(), "render with debug draw disabled");
				context.check(SceneTester::max_difference(baseline, test.pixels()) == 0, "an empty debug frame is bit-identical to the baseline");
			});

		register_render_check("debug_pass_cross_backend", [](RenderCheckContext& context)
			{
				SceneTester test(context, 96);
				dark_scene(test);
				test.add_instance(test.add_mesh(make_test_cube(test.add_material(test.color_material(glm::vec3(0.8f, 0.4f, 0.1f))))), glm::mat4(1.0f));
				test.add_light(test.sun(glm::vec3(-0.3f, -0.5f, -1.0f)));
				DebugDraw& draw = test.debug();
				draw.box(glm::vec3(-1.5f), glm::vec3(1.5f), glm::vec4(0.2f, 1.0f, 0.2f, 1.0f), DebugDepth::Tested, 1.0f, 100.0f);
				draw.sphere(glm::vec3(0.0f), 2.0f, glm::vec4(0.3f, 0.5f, 1.0f, 1.0f), DebugDepth::Faded, 2.0f, 100.0f);
				draw.arrow(glm::vec3(-2.0f, -2.0f, 2.0f), glm::vec3(2.0f, 2.0f, 2.0f), glm::vec4(1.0f, 1.0f, 0.0f, 1.0f), DebugDepth::AlwaysOnTop, 3.0f, 100.0f);
				draw.quad(glm::vec3(1.5f, -1.5f, 0.0f), glm::vec3(0.5f, 0.0f, 0.0f), glm::vec3(0.0f, 0.5f, 0.0f), glm::vec4(1.0f, 0.0f, 1.0f, 0.5f), DebugDepth::Faded, 100.0f);
				draw.line(glm::vec3(-3.0f, 0.0f, 2.0f), glm::vec3(0.0f, 0.0f, -9.0f), WHITE, DebugDepth::AlwaysOnTop, 1.0f, 100.0f); // runs behind the eye plane's far side
				context.check(test.render(), "render the debug scene");
				compare_across_apis(context, "debug", test.pixels());
			});

		register_render_check("debug_pass_resize", [](RenderCheckContext& context)
			{
				SceneTester test(context, SIZE);
				dark_scene(test);
				test.debug().box(glm::vec3(-1.0f), glm::vec3(1.0f), WHITE, DebugDepth::AlwaysOnTop, 2.0f, 1000.0f);
				test.debug().sphere(glm::vec3(0.0f), 1.5f, WHITE, DebugDepth::Faded, 1.0f, 1000.0f);
				context.check(test.render(), "render with shapes");

				test.set_size(1);
				context.check(test.render(), "render at 1x1");
				test.set_size(17);
				context.check(test.render(), "render at 17x17");
				test.set_size(SIZE);
				context.check(test.render(), "render at the original size");

				int white = 0;
				for (int y = 0; y < static_cast<int>(SIZE); y++)
				{
					for (int x = 0; x < static_cast<int>(SIZE); x++)
					{
						white += is_white(test.pixel(x, y)) ? 1 : 0;
					}
				}
				context.check(white > 50, "the shapes are still drawn after the resizes");
			});
	}
}
