#include <pch.h>
#include "scene_tester.h"

#include <fstream>

#include "../ibl/environment_pack.h"

// Checks of a whole scene with an environment: the same image on every API, a changed asset or setting showing on the next frames, and a
// scene replaced while its environment was still uploading.
namespace wiwa
{
	namespace
	{
		// A sky that is blue above and warm below, with a bright patch to one side.
		EnvironmentData gradient_environment(float strength)
		{
			const CubeMap cube = make_cube(32, [strength](const glm::vec3& d)
				{
					const float up = d.y * 0.5f + 0.5f;
					const glm::vec3 sky = glm::mix(glm::vec3(0.9f, 0.6f, 0.4f), glm::vec3(0.2f, 0.4f, 1.0f), up);
					const float patch = std::pow(std::max(glm::dot(d, glm::normalize(glm::vec3(1.0f, 0.4f, 0.3f))), 0.0f), 24.0f);
					return (sky + glm::vec3(6.0f, 5.0f, 4.0f) * patch) * strength;
				});
			return pack_environment(build_environment(cube, 1, 8));
		}

		EnvironmentData flat_environment(glm::vec3 color)
		{
			return pack_environment(build_environment(make_cube(32, [color](const glm::vec3&) { return color; }), 1, 8));
		}

		void reference_scene(SceneTester& test, SceneSettings settings)
		{
			test.scene().set_settings(settings);
			test.set_camera(glm::vec3(3.0f, 2.5f, 4.0f), glm::vec3(0.0f));
			test.add_light(test.sun(glm::normalize(glm::vec3(-0.4f, -0.8f, -0.3f)), 2.0f, true));
			test.add_instance(test.add_mesh(make_test_cube(test.add_material(test.color_material(glm::vec3(0.8f, 0.5f, 0.3f), 0.4f, 0.0f)))), glm::mat4(1.0f));
			test.add_instance(test.add_mesh(make_test_cube(test.add_material(test.color_material(glm::vec3(0.9f), 0.2f, 1.0f)))), glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.6f, 0.0f)) * glm::scale(glm::mat4(1.0f), glm::vec3(4.0f, 0.2f, 4.0f)));
		}

		int average_luma(SceneTester& test)
		{
			u64 total = 0;
			const std::vector<u8> pixels = test.pixels();
			for (size_t i = 0; i + 3 < pixels.size(); i += 4)
			{
				total += static_cast<u64>(pixels[i]) + pixels[i + 1] + pixels[i + 2];
			}
			return static_cast<int>(total / std::max<size_t>(pixels.size() / 4 * 3, 1));
		}

		// Writes this API's image and compares it with the other APIs' images that are already there.
		void compare_with_other_apis(RenderCheckContext& context, const std::string& name, const std::vector<u8>& pixels)
		{
			namespace fs = std::filesystem;
			const std::string own = std::string(graphics_api_name(context.Api));
			std::error_code error;
			fs::create_directories("Library", error);
			{
				std::ofstream file("Library/render_test_" + name + "_" + own + ".rgba", std::ios::binary | std::ios::trunc);
				file.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
			}

			for (const char* other : { "vulkan", "d3d11", "d3d12" })
			{
				if (own == other)
				{
					continue;
				}
				std::ifstream file(std::string("Library/render_test_") + name + "_" + other + ".rgba", std::ios::binary);
				if (!file)
				{
					continue;
				}
				std::vector<u8> theirs((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
				if (theirs.size() != pixels.size())
				{
					continue;
				}

				u64 total = 0;
				int worst = 0;
				for (size_t i = 0; i < pixels.size(); i++)
				{
					if ((i & 3) == 3)
					{
						continue;
					}
					const int difference = std::abs(static_cast<int>(pixels[i]) - static_cast<int>(theirs[i]));
					total += static_cast<u64>(difference);
					worst = std::max(worst, difference);
				}
				const double mean = static_cast<double>(total) / (static_cast<double>(pixels.size()) * 0.75);
				WI_LOG(Info, "RenderTest", "{0}: {1} against {2}: mean difference {3:.3f}, largest {4}", name, own, other, mean, worst);
				context.check(mean < 3.0 && worst <= 40, name + " matches " + other);
			}
		}
	}

	void register_environment_scene_checks()
	{
		register_render_check("environment_cross_backend", [](RenderCheckContext& context)
			{
				{
					SceneTester test(context, 96);
					SceneSettings settings;
					settings.Source = EnvironmentSource::File;
					settings.EnvironmentAsset = test.renderer().get_environments().add_inline_environment(gradient_environment(1.0f));
					settings.RotationDegrees = 30.0f;
					reference_scene(test, settings);
					context.check(test.render(), "render the scene with a file environment");
					compare_with_other_apis(context, "environment_file", test.pixels());
				}
				{
					SceneTester test(context, 96);
					SceneSettings settings;
					settings.Source = EnvironmentSource::Atmosphere;
					reference_scene(test, settings);
					context.check(test.render(), "render the scene with the atmosphere");
					compare_with_other_apis(context, "environment_atmosphere", test.pixels());
				}
			});

		register_render_check("environment_hot_reload_scene", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				EnvironmentCache& environments = test.renderer().get_environments();
				SceneSettings settings;
				settings.Source = EnvironmentSource::File;
				settings.EnvironmentAsset = environments.add_inline_environment(flat_environment(glm::vec3(4.0f, 0.0f, 0.0f)));
				test.scene().set_settings(settings);
				test.set_camera(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
				context.check(test.render(), "render with the red environment");
				const glm::ivec3 red = test.pixel(32, 32);
				context.check(red.r > 150 && red.g < 40 && red.b < 40, "the sky is red");

				environments.set_inline_environment(settings.EnvironmentAsset, flat_environment(glm::vec3(0.0f, 0.0f, 4.0f)));
				environments.invalidate(settings.EnvironmentAsset.Guid);
				context.check(test.render(), "render after the environment was cooked again");
				const glm::ivec3 blue = test.pixel(32, 32);
				context.check(blue.b > 150 && blue.r < 40 && blue.g < 40, "the new environment shows on the next frames");
			});

		register_render_check("settings_change_live", [](RenderCheckContext& context)
			{
				SceneTester test(context, 64);
				SceneSettings settings;
				settings.Source = EnvironmentSource::File;
				settings.EnvironmentAsset = test.renderer().get_environments().add_inline_environment(gradient_environment(0.25f));
				reference_scene(test, settings);
				context.check(test.render(), "render");
				const int normal = average_luma(test);

				settings.ExposureEv = 1.0f;
				test.scene().set_settings(settings);
				context.check(test.render(2), "render with one more stop");
				const int brighter = average_luma(test);
				WI_LOG(Info, "RenderTest", "settings_change_live luma {0} -> {1} with +1 EV", normal, brighter);
				context.check(brighter > normal + 2, "raising the exposure brightens the next frame");

				// the source cycles through every kind without a GPU error
				settings.ExposureEv = 0.0f;
				bool ok = true;
				for (EnvironmentSource source : { EnvironmentSource::Atmosphere, EnvironmentSource::None, EnvironmentSource::File, EnvironmentSource::None })
				{
					settings.Source = source;
					test.scene().set_settings(settings);
					ok = test.render(2) && ok;
				}
				context.check(ok, "the source can change File, Atmosphere, None and back without an error");
				context.check(test.pixels().size() > 0 && average_luma(test) > 0, "and the scene still renders");
			});

		register_render_check("environment_scene_swap", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				EnvironmentCache& environments = test.renderer().get_environments();
				const AssetRef green = environments.add_inline_environment(flat_environment(glm::vec3(0.0f, 4.0f, 0.0f)));
				const AssetRef red = environments.add_inline_environment(flat_environment(glm::vec3(4.0f, 0.0f, 0.0f)));
				test.set_camera(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1.0f));

				SceneSettings settings;
				settings.Source = EnvironmentSource::File;
				settings.EnvironmentAsset = green;
				test.scene().set_settings(settings);
				test.frame(); // its upload has begun
				settings.EnvironmentAsset = red; // the scene is replaced by another before the first one finished
				test.scene().set_settings(settings);
				context.check(test.render(), "render");
				const glm::ivec3 pixel = test.pixel(32, 32);
				context.check(pixel.r > 150 && pixel.g < 40, "only the new scene's environment shows");
			});
	}
}
