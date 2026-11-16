#include <pch.h>
#include "scene_tester.h"

#include <renderer/ibl/environment_pack.h>

// Checks of the sky pass: the environment drawn behind the scene (and only behind it).
namespace wiwa
{
	namespace
	{
		// Each face of the cube a different colour, by the axis the direction points along most.
		EnvironmentData six_color_environment()
		{
			const CubeMap cube = make_cube(32, [](const glm::vec3& d)
				{
					const glm::vec3 a = glm::abs(d);
					if (a.x >= a.y && a.x >= a.z)
					{
						return d.x > 0.0f ? glm::vec3(4, 0, 0) : glm::vec3(0, 4, 0);   // +X red, -X green
					}
					if (a.y >= a.z)
					{
						return d.y > 0.0f ? glm::vec3(0, 0, 4) : glm::vec3(4, 4, 0);   // +Y blue, -Y yellow
					}
					return d.z > 0.0f ? glm::vec3(4, 0, 4) : glm::vec3(0, 4, 4);       // +Z magenta, -Z cyan
				});
			return pack_environment(build_environment(cube, 1, 8));
		}

		// 'r', 'g', 'b', 'y', 'm', 'c' by which channels are on, '?' for anything else.
		char color_name(glm::ivec3 pixel)
		{
			const bool r = pixel.r > 150;
			const bool g = pixel.g > 150;
			const bool b = pixel.b > 150;
			const bool rOff = pixel.r < 40;
			const bool gOff = pixel.g < 40;
			const bool bOff = pixel.b < 40;
			if (r && gOff && bOff) return 'r';
			if (g && rOff && bOff) return 'g';
			if (b && rOff && gOff) return 'b';
			if (r && g && bOff) return 'y';
			if (r && b && gOff) return 'm';
			if (g && b && rOff) return 'c';
			return '?';
		}

		void use_sky(SceneTester& test, AssetRef environment, float rotation = 0.0f, BackgroundMode background = BackgroundMode::Sky)
		{
			SceneSettings settings;
			settings.Source = EnvironmentSource::File;
			settings.EnvironmentAsset = environment;
			settings.RotationDegrees = rotation;
			settings.Background = background;
			test.scene().set_settings(settings);
		}

		char view_color(RenderCheckContext& context, glm::vec3 direction, float rotation)
		{
			SceneTester test(context);
			use_sky(test, test.renderer().get_environments().add_inline_environment(six_color_environment()), rotation);
			test.set_camera(glm::vec3(0.0f), direction);
			test.render();
			const glm::ivec3 p = test.pixel(32, 32);
			return color_name(p);
		}
	}

	void register_sky_checks()
	{
		register_render_check("sky_shows_environment", [](RenderCheckContext& context)
			{
				const struct { glm::vec3 Direction; char Expected; const char* Name; } views[] = {
					{ { 1, 0, 0 }, 'r', "+X is red" },     { { -1, 0, 0 }, 'g', "-X is green" },
					{ { 0.01f, 1, 0 }, 'b', "+Y is blue" }, { { 0.01f, -1, 0 }, 'y', "-Y is yellow" },
					{ { 0, 0, 1 }, 'm', "+Z is magenta" }, { { 0, 0, -1 }, 'c', "-Z is cyan" },
				};
				for (const auto& view : views)
				{
					const char found = view_color(context, view.Direction, 0.0f);
					context.check(found == view.Expected, std::string(view.Name) + ", saw '" + found + "'");
				}
			});

		register_render_check("sky_rotation", [](RenderCheckContext& context)
			{
				// What was at +Z is seen at +X when the environment is turned 90 degrees (the same sense as the lighting).
				const char found = view_color(context, glm::vec3(1, 0, 0), 90.0f);
				context.check(found == 'm', std::string("turned by 90 degrees, +X shows what was at +Z, saw '") + found + "'");
			});

		register_render_check("sky_not_over_geometry", [](RenderCheckContext& context)
			{
				auto render = [&](BackgroundMode background, std::vector<u8>& pixels)
				{
					SceneTester test(context);
					use_sky(test, test.renderer().get_environments().add_inline_environment(six_color_environment()), 0.0f, background);
					test.set_camera(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f, 0.0f, 0.0f));
					test.add_instance(test.add_mesh(make_test_cube(test.add_material(test.color_material(glm::vec3(0.5f))))), glm::mat4(1.0f));
					const bool ok = test.render();
					pixels = test.pixels();
					return ok;
				};
				std::vector<u8> withSky;
				std::vector<u8> solid;
				context.check(render(BackgroundMode::Sky, withSky) && render(BackgroundMode::SolidColor, solid), "render both");
				const size_t center = (32 * 64 + 32) * 4;
				context.check(withSky[center] == solid[center] && withSky[center + 1] == solid[center + 1] && withSky[center + 2] == solid[center + 2], "the cube is the same with and without the sky behind it");
				const size_t corner = (2 * 64 + 2) * 4;
				context.check(withSky[corner] != solid[corner] || withSky[corner + 1] != solid[corner + 1] || withSky[corner + 2] != solid[corner + 2], "but the corner shows the sky");
			});

		register_render_check("sky_solid_background", [](RenderCheckContext& context)
			{
				SceneTester plain(context);
				SceneSettings none;
				none.BackgroundColor = glm::vec3(0.3f, 0.1f, 0.2f);
				plain.scene().set_settings(none);
				context.check(plain.render(2), "render without an environment");
				const glm::ivec3 expected = plain.pixel(2, 2);

				SceneTester test(context);
				use_sky(test, test.renderer().get_environments().add_inline_environment(six_color_environment()), 0.0f, BackgroundMode::SolidColor);
				SceneSettings settings = test.scene().settings();
				settings.BackgroundColor = glm::vec3(0.3f, 0.1f, 0.2f);
				test.scene().set_settings(settings);
				context.check(test.render(), "render with the environment and a solid background");
				context.check(test.pixel(2, 2) == expected, "a solid background shows exactly its colour");
			});

		register_render_check("sky_missing_asset", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				SceneSettings settings;
				settings.Source = EnvironmentSource::File;
				settings.EnvironmentAsset.Guid = AssetGuid::generate();
				test.scene().set_settings(settings);
				for (int i = 0; i < 3000 && test.renderer().get_environments().get_stats().Failed == 0; i++)
				{
					test.frame();
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
				context.check(test.render(), "render");
				SceneTester plain(context);
				context.check(plain.render(), "render without an environment");
				context.check(test.pixel(2, 2) == plain.pixel(2, 2), "a missing environment shows the background colour");
			});

		register_render_check("sky_blended_over_sky", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				use_sky(test, test.renderer().get_environments().add_inline_environment(six_color_environment()));
				test.set_camera(glm::vec3(0.0f), glm::vec3(-1.0f, 0.0f, 0.0f)); // looking at the green side
				MaterialData glass;
				glass.BaseColorFactor = glm::vec4(1.0f, 0.0f, 0.0f, 0.5f);
				glass.Alpha = AlphaMode::Blend;
				test.add_instance(test.add_mesh(make_test_box(glm::vec3(0.02f, 1.0f, 1.0f), test.add_material(glass))), glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f, 0.0f, 0.0f)));
				context.check(test.render(), "render");
				const glm::ivec3 mixed = test.pixel(32, 32);
				WI_LOG(Info, "RenderTest", "sky_blended_over_sky ({0}, {1}, {2})", mixed.r, mixed.g, mixed.b);
				context.check(mixed.r > 40 && mixed.g > 40, "a half-transparent quad shows its colour over the sky");
			});
	}
}
