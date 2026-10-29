#include <pch.h>
#include "scene_tester.h"

#include "../scene/frustum_culler.h"

// Checks of the depth prepass and the forward pass: scenes rendered through the whole renderer and read back.
namespace wiwa
{
	void register_forward_checks()
	{
		register_render_check("frustum_culling", [](RenderCheckContext& context)
			{
				const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
				const glm::mat4 projection = glm::perspectiveRH_ZO(glm::radians(60.0f), 1.0f, 0.1f, 50.0f);
				const glm::mat4 viewProjection = projection * view;
				const glm::vec3 low(-0.5f);
				const glm::vec3 high(0.5f);
				auto at = [](glm::vec3 position) { return glm::translate(glm::mat4(1.0f), position); };

				context.check(FrustumCuller::is_visible(viewProjection, at({ 0, 0, 0 }), low, high), "a box in front of the camera is visible");
				context.check(!FrustumCuller::is_visible(viewProjection, at({ 0, 0, 10 }), low, high), "a box behind the camera is not");
				context.check(!FrustumCuller::is_visible(viewProjection, at({ 40, 0, 0 }), low, high), "a box far to the side is not");
				context.check(!FrustumCuller::is_visible(viewProjection, at({ 0, -40, 0 }), low, high), "a box far below is not");
				context.check(!FrustumCuller::is_visible(viewProjection, at({ 0, 0, -100 }), low, high), "a box beyond the far plane is not");
				context.check(FrustumCuller::is_visible(viewProjection, at({ 0, 0, 5.0f + 0.1f }), low, high), "a box straddling the near plane is visible");
				context.check(FrustumCuller::is_visible(viewProjection, at({ 2.9f, 0, 0 }), low, high), "a box straddling a side plane is visible");

				glm::mat4 broken = at({ 0, 0, 0 });
				broken[3][0] = std::numeric_limits<float>::quiet_NaN();
				context.check(!FrustumCuller::is_visible(viewProjection, broken, low, high), "a NaN matrix is not visible");
				context.check(!FrustumCuller::is_visible(viewProjection, at({ 0, 0, 0 }), low, glm::vec3(std::numeric_limits<float>::infinity())), "an infinite box is not visible");

				// A scaled and rotated box is tested at its world size.
				const glm::mat4 scaled = at({ 6.0f, 0, 0 }) * glm::scale(glm::mat4(1.0f), glm::vec3(4.0f));
				context.check(FrustumCuller::is_visible(viewProjection, scaled, low, high), "a big box reaching into view is visible");
			});

		register_render_check("forward_flat_color", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				context.check(test.render(2), "render the empty scene");
				const std::vector<u8> empty = test.pixels();
				const glm::ivec3 background = test.pixel(1, 1);

				const AssetRef material = test.add_material(test.color_material(glm::vec3(1.0f, 0.0f, 0.0f)));
				const AssetRef cube = test.add_mesh(make_test_cube(material));
				test.add_instance(cube, glm::mat4(1.0f));
				context.check(test.render(), "render with the cube");

				const glm::ivec3 center = test.pixel(32, 32);
				context.check(center.r > 3 * center.g && center.r > 3 * center.b && center.r > 40, "the cube is red");
				context.check(test.pixel(1, 1) == background, "the corner is still the background");
				context.check(SceneTester::max_difference(test.pixels(), empty) > 40, "the image changed");
				WI_LOG(Info, "RenderTest", "forward_flat_color center ({0}, {1}, {2}) corner ({3}, {4}, {5})", center.r, center.g, center.b, background.r, background.g, background.b);
			});

		register_render_check("forward_directional_lighting", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.set_camera(glm::vec3(3.0f, 2.0f, 5.0f), glm::vec3(0.0f));
				const AssetRef material = test.add_material(test.color_material(glm::vec3(1.0f)));
				test.add_instance(test.add_mesh(make_test_cube(material)), glm::mat4(1.0f));
				test.add_light(test.sun(glm::vec3(0.0f, 0.0f, -1.0f))); // from +Z: lights the +Z face, leaves the +X face dark
				context.check(test.render(), "render");

				const int lit = SceneTester::luma(test.pixel(test.to_pixel(glm::vec3(0.0f, 0.0f, 1.0f))));
				const int away = SceneTester::luma(test.pixel(test.to_pixel(glm::vec3(1.0f, 0.0f, 0.0f))));
				WI_LOG(Info, "RenderTest", "forward_directional_lighting lit {0} away {1}", lit, away);
				context.check(away > 0 && lit > 2 * away, "the face toward the light is more than twice as bright as the one away from it");
			});

		register_render_check("forward_depth_equal", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.set_camera(glm::vec3(3.0f, 2.0f, 5.0f), glm::vec3(0.0f));
				const AssetRef material = test.add_material(test.color_material(glm::vec3(0.8f, 0.3f, 0.2f), 0.5f));
				const AssetRef cube = test.add_mesh(make_test_cube(material));
				test.add_instance(cube, glm::mat4(1.0f));
				test.add_light(test.sun(glm::vec3(-0.4f, -0.6f, -0.7f)));
				context.check(test.render(), "render the cube");
				const std::vector<u8> single = test.pixels();

				// The same cube again, a hair further from the camera: it draws behind the first and must change nothing.
				test.add_instance(cube, glm::translate(glm::mat4(1.0f), glm::vec3(-0.0004f, -0.0003f, -0.0005f)));
				test.add_instance(cube, glm::mat4(1.0f));
				context.check(test.render(), "render with the copies");
				const int difference = SceneTester::max_difference(test.pixels(), single);
				WI_LOG(Info, "RenderTest", "forward_depth_equal difference {0}", difference);
				context.check(difference <= 3, "the image is the same with extra copies behind and on top");
			});

		register_render_check("forward_unready_skipped", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				context.check(test.render(2), "render the empty scene");
				const std::vector<u8> empty = test.pixels();

				const AssetRef material = test.add_material(test.color_material(glm::vec3(0.0f, 1.0f, 0.0f)));
				test.add_instance(test.add_mesh(make_test_cube(material)), glm::mat4(1.0f));
				context.check(test.frame() && test.read(), "the first frame with the new instance");
				context.check(SceneTester::max_difference(test.pixels(), empty) == 0, "its mesh is not uploaded yet, so nothing is drawn");

				context.check(test.render(), "and later");
				context.check(SceneTester::max_difference(test.pixels(), empty) > 40, "the cube shows once uploaded");
			});
	}
}
