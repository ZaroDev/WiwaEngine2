#include <pch.h>
#include "render_test.h"

#include <glm/gtc/packing.hpp>

#include "../environment/environment_cache.h"
#include "../ibl/environment_pack.h"
#include "../render_world.h"
#include "../renderer.h"
#include "scene_tester.h"

// Checks of the environment cache: uploading, the BRDF table, failures, reloads. Each check builds its own queue and cache.
namespace wiwa
{
	namespace
	{
		struct EnvironmentFixture
		{
			EnvironmentFixture(RenderCheckContext& context, size_t budget, bool withJobs = false)
			{
				Queue.initialize(context.Device, context.HasCopyQueue, budget);
				if (withJobs)
				{
					Jobs = std::make_unique<JobSystem>(1);
				}
				Cache = std::make_unique<EnvironmentCache>(context.Device, context.World.get_renderer().get_assets(), &Queue, Jobs.get());
			}

			~EnvironmentFixture()
			{
				Cache.reset();
				Jobs.reset();
				Queue.shutdown();
			}

			void frame()
			{
				Queue.begin_frame();
				Cache->update();
				Queue.submit();
			}

			template<typename Predicate>
			bool pump_until(Predicate done, u32 maxFrames = 60, u32 sleepMs = 1)
			{
				for (u32 i = 0; i < maxFrames && !done(); i++)
				{
					frame();
					std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
				}
				return done();
			}

			UploadQueue Queue{};
			std::unique_ptr<JobSystem> Jobs{};
			std::unique_ptr<EnvironmentCache> Cache{};
		};

		// A 64x64 environment: red where the direction points mostly along +X, otherwise the other color.
		EnvironmentData make_environment(glm::vec3 plusX, glm::vec3 other)
		{
			const CubeMap cube = make_cube(64, [&](const glm::vec3& d)
				{
					return (d.x > std::abs(d.y) && d.x > std::abs(d.z)) ? plusX : other;
				});
			return pack_environment(build_environment(cube, 1, 8));
		}

		// One slice of one mip of a half-float texture, as floats (RGBA, or RG for the BRDF table: channels says how many).
		bool read_halves(RenderCheckContext& context, nvrhi::ITexture* texture, u32 slice, u32 mip, u32 channels, std::vector<float>& out, u32& width)
		{
			std::vector<u8> bytes;
			u32 height = 0;
			if (!read_texture_slice_bytes(context.Device, texture, slice, mip, bytes, width, height, channels * 2))
			{
				return false;
			}
			out.clear();
			for (size_t i = 0; i + 1 < bytes.size(); i += 2)
			{
				u16 half;
				std::memcpy(&half, &bytes[i], 2);
				out.push_back(glm::unpackHalf1x16(half));
			}
			return true;
		}
	}

	void register_environment_checks()
	{

		// Image-based lighting in the forward shader, through the whole renderer.
		auto light_by = [](SceneTester& test, EnvironmentData environment, float rotationDegrees = 0.0f, float brightness = 1.0f)
		{
			SceneSettings settings;
			settings.Source = EnvironmentSource::File;
			settings.EnvironmentAsset = test.renderer().get_environments().add_inline_environment(std::move(environment));
			settings.AmbientIntensity = 1.0f;
			settings.RotationDegrees = rotationDegrees;
			settings.Brightness = brightness;
			test.scene().set_settings(settings);
		};
		auto uniform_environment = [](float value)
		{
			return pack_environment(build_environment(make_cube(64, [value](const glm::vec3&) { return glm::vec3(value); }), 1, 8));
		};
		auto white_cube = [](SceneTester& test, float roughness, float metallic)
		{
			test.add_instance(test.add_mesh(make_test_cube(test.add_material(test.color_material(glm::vec3(1.0f), roughness, metallic)))), glm::mat4(1.0f));
		};

		register_render_check("ibl_uniform_diffuse", [=](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.set_camera(glm::vec3(3.0f, 2.0f, 5.0f), glm::vec3(0.0f));
				white_cube(test, 1.0f, 0.0f);
				context.check(test.render(), "render with the hemisphere");
				const int hemisphere = SceneTester::luma(test.pixel(test.to_pixel(glm::vec3(0.0f, 0.0f, 1.0f))));

				light_by(test, uniform_environment(0.5f));
				context.check(test.render(), "render with a uniform environment");
				const int front = SceneTester::luma(test.pixel(test.to_pixel(glm::vec3(0.0f, 0.0f, 1.0f))));
				const int side = SceneTester::luma(test.pixel(test.to_pixel(glm::vec3(1.0f, 0.0f, 0.0f))));
				const int top = SceneTester::luma(test.pixel(test.to_pixel(glm::vec3(0.0f, 1.0f, 0.0f))));
				WI_LOG(Info, "RenderTest", "ibl_uniform_diffuse hemisphere {0} faces {1} {2} {3}", hemisphere, front, side, top);
				context.check(std::abs(front - side) <= 4 && std::abs(front - top) <= 4, "three faces seen at different angles are equally lit");
				context.check(front > 60 && std::abs(front - hemisphere) > 10, "and the image is not the one of the hemisphere ambient");
			});

		register_render_check("ibl_rotation", [=](RenderCheckContext& context)
			{
				const CubeMap half = make_cube(64, [](const glm::vec3& d) { return d.x > 0.0f ? glm::vec3(3.0f) : glm::vec3(0.02f); });
				auto side_luma = [&](float rotation)
				{
					SceneTester test(context);
					test.set_camera(glm::vec3(3.0f, 2.0f, 5.0f), glm::vec3(0.0f));
					white_cube(test, 1.0f, 0.0f);
					light_by(test, pack_environment(build_environment(half, 1, 8)), rotation);
					test.render();
					return SceneTester::luma(test.pixel(test.to_pixel(glm::vec3(1.0f, 0.0f, 0.0f))));
				};
				const int straight = side_luma(0.0f);
				const int turned = side_luma(180.0f);
				WI_LOG(Info, "RenderTest", "ibl_rotation +X face: rotation 0 -> {0}, 180 -> {1}", straight, turned);
				context.check(straight > 2 * turned, "the face toward the bright half is brighter, and the rotation turns the light away");
			});

		register_render_check("ibl_specular_reflection", [=](RenderCheckContext& context)
			{
				const CubeMap red_front = make_cube(64, [](const glm::vec3& d) { return (d.z > std::abs(d.x) && d.z > std::abs(d.y)) ? glm::vec3(4.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 0.3f); });
				auto saturation = [&](float roughness)
				{
					SceneTester test(context);
					test.set_camera(glm::vec3(0.0f, 0.0f, 5.0f), glm::vec3(0.0f));
					white_cube(test, roughness, 1.0f);
					light_by(test, pack_environment(build_environment(red_front, 1, 8)));
					test.render();
					const glm::ivec3 pixel = test.pixel(32, 32);
					return static_cast<float>(pixel.r) / static_cast<float>(std::max(pixel.r + pixel.g + pixel.b, 1));
				};
				const float mirror = saturation(0.1f);
				const float rough = saturation(1.0f);
				WI_LOG(Info, "RenderTest", "ibl_specular_reflection red share: smooth {0:.2f}, rough {1:.2f}", mirror, rough);
				context.check(mirror > 0.8f, "a smooth metal facing the red part of the sky reflects it");
				context.check(rough < mirror - 0.1f, "a rough one blurs the reflection with the rest of the sky");
			});

		register_render_check("ibl_brightness", [=](RenderCheckContext& context)
			{
				auto luma = [&](float brightness)
				{
					SceneTester test(context);
					test.set_camera(glm::vec3(3.0f, 2.0f, 5.0f), glm::vec3(0.0f));
					white_cube(test, 1.0f, 0.0f);
					light_by(test, uniform_environment(0.2f), 0.0f, brightness);
					test.render();
					return SceneTester::luma(test.pixel(test.to_pixel(glm::vec3(0.0f, 0.0f, 1.0f))));
				};
				context.check(luma(2.0f) > luma(1.0f) + 10, "doubling the brightness brightens the lit pixel");
			});

		register_render_check("ibl_missing_falls_back", [=](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.set_camera(glm::vec3(3.0f, 2.0f, 5.0f), glm::vec3(0.0f));
				white_cube(test, 0.5f, 0.0f);
				context.check(test.render(), "render with no environment");
				const std::vector<u8> none = test.pixels();

				SceneSettings settings;
				settings.Source = EnvironmentSource::File;
				settings.EnvironmentAsset.Guid = AssetGuid::generate();
				test.scene().set_settings(settings);
				for (int i = 0; i < 3000 && test.renderer().get_environments().get_stats().Failed == 0; i++)
				{
					test.frame();
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
				context.check(test.renderer().get_environments().status(settings.EnvironmentAsset).rfind("failed", 0) == 0, "the failure is reported");
				context.check(test.render() && SceneTester::max_difference(test.pixels(), none) == 0, "the scene is lit as if it had no environment");
			});
		register_render_check("environment_upload", [](RenderCheckContext& context)
			{
				EnvironmentFixture fixture(context, 8 * 1024 * 1024);
				const EnvironmentData source = make_environment(glm::vec3(4.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 4.0f));
				const AssetRef ref = fixture.Cache->add_inline_environment(source);
				context.check(fixture.Cache->get(ref) == nullptr, "not uploaded before the first update");
				context.check(fixture.pump_until([&] { return fixture.Cache->get(ref) != nullptr; }), "uploaded within a few frames");

				const GpuEnvironment* gpu = fixture.Cache->get(ref);
				if (gpu == nullptr)
				{
					return;
				}
				context.check(gpu->MipCount == source.Mips.size() && gpu->SH == source.SH, "the mip count and the SH");
				context.check(gpu->Cube->getDesc().arraySize == 6 && gpu->Cube->getDesc().dimension == nvrhi::TextureDimension::TextureCube, "a cube texture");

				std::vector<float> texels;
				u32 width = 0;
				context.check(read_halves(context, gpu->Cube, 0, 0, 4, texels, width) && width == 64, "read +X back");
				if (texels.size() == 64 * 64 * 4)
				{
					const size_t center = (32 * 64 + 32) * 4;
					context.check(texels[center] > 3.0f && texels[center + 2] < 0.5f, "the +X face is red");
				}
				context.check(read_halves(context, gpu->Cube, 1, 0, 4, texels, width) && texels.size() == 64 * 64 * 4, "read -X back");
				if (texels.size() == 64 * 64 * 4)
				{
					const size_t center = (32 * 64 + 32) * 4;
					context.check(texels[center] < 0.5f && texels[center + 2] > 3.0f, "the -X face is blue");
				}
				context.check(read_halves(context, gpu->Cube, 0, 2, 4, texels, width) && width == 16, "a smaller mip has its size");
			});

		register_render_check("environment_brdf_lut", [](RenderCheckContext& context)
			{
				EnvironmentFixture fixture(context, 8 * 1024 * 1024, true);
				context.check(fixture.pump_until([&] { return fixture.Cache->brdf_lut() != nullptr; }, 2000, 5), "the table is built and uploaded");
				nvrhi::ITexture* table = fixture.Cache->brdf_lut();
				if (table == nullptr)
				{
					return;
				}
				std::vector<float> values;
				u32 width = 0;
				context.check(read_halves(context, table, 0, 0, 2, values, width) && width == BRDF_LUT_SIZE, "read it back");
				if (values.size() == static_cast<size_t>(BRDF_LUT_SIZE) * BRDF_LUT_SIZE * 2)
				{
					const size_t smoothHeadOn = (0 * BRDF_LUT_SIZE + (BRDF_LUT_SIZE - 1)) * 2;
					context.check(values[smoothHeadOn] + values[smoothHeadOn + 1] > 0.9f, "a smooth surface seen head on reflects almost everything");
					bool bounded = true;
					for (float value : values)
					{
						bounded = bounded && std::isfinite(value) && value >= 0.0f && value <= 1.05f;
					}
					context.check(bounded, "every value is finite and in range");
				}
				context.check(fixture.Cache->get_stats().BrdfReady, "the stats say so");
			});

		register_render_check("environment_failed", [](RenderCheckContext& context)
			{
				EnvironmentFixture fixture(context, 1024 * 1024);
				AssetRef missing;
				missing.Guid = AssetGuid::generate();
				context.check(fixture.Cache->get(missing) == nullptr, "null at first");
				context.check(fixture.pump_until([&] { return fixture.Cache->get_stats().Failed == 1; }, 3000), "the failure is noticed");
				context.check(fixture.Cache->get(missing) == nullptr, "and stays null");
				fixture.frame();
				fixture.frame();
				context.check(fixture.Cache->get_stats().Failed == 1, "counted once");
				context.check(fixture.Cache->status(missing).rfind("failed", 0) == 0, "the status says why: " + fixture.Cache->status(missing));
				context.check(fixture.Cache->status(AssetRef{}) == "none", "no reference, no status");
			});

		register_render_check("environment_wrong_type", [](RenderCheckContext& context)
			{
				EnvironmentFixture fixture(context, 1024 * 1024);
				const std::optional<AssetRef> shader = context.World.get_renderer().get_assets()->find_asset("engine/shaders/imgui.hlsl");
				context.check(shader.has_value(), "a shader asset to misuse");
				if (!shader)
				{
					return;
				}
				context.check(fixture.Cache->get(*shader) == nullptr, "null at first");
				context.check(fixture.pump_until([&] { return fixture.Cache->get_stats().Failed == 1; }, 3000), "an asset of another type fails");
				context.check(fixture.Cache->get(*shader) == nullptr, "and gives nothing");
			});

		register_render_check("environment_invalidate", [](RenderCheckContext& context)
			{
				EnvironmentFixture fixture(context, 8 * 1024 * 1024);
				const AssetRef ref = fixture.Cache->add_inline_environment(make_environment(glm::vec3(4.0f, 0.0f, 0.0f), glm::vec3(0.0f)));
				fixture.Cache->get(ref);
				context.check(fixture.pump_until([&] { return fixture.Cache->get(ref) != nullptr; }), "uploaded");

				fixture.Cache->set_inline_environment(ref, make_environment(glm::vec3(0.0f, 4.0f, 0.0f), glm::vec3(0.0f)));
				fixture.Cache->invalidate(ref.resolved());
				context.check(fixture.Cache->get(ref) != nullptr, "the old version is used while the new one is on its way");
				context.check(fixture.pump_until([&] { return fixture.Cache->get_stats().Reloads == 1; }), "the reload is counted");

				std::vector<float> texels;
				u32 width = 0;
				const GpuEnvironment* gpu = fixture.Cache->get(ref);
				context.check(gpu != nullptr && read_halves(context, gpu->Cube, 0, 0, 4, texels, width), "read the new +X face");
				if (texels.size() == 64 * 64 * 4)
				{
					const size_t center = (32 * 64 + 32) * 4;
					context.check(texels[center + 1] > 3.0f && texels[center] < 0.5f, "it is green now");
				}

				// Two invalidations in a row leave one valid environment.
				fixture.Cache->invalidate(ref.resolved());
				fixture.Cache->invalidate(ref.resolved());
				context.check(fixture.Cache->get_stats().Pending == 1, "one pending reload, not two");
				context.check(fixture.pump_until([&] { return fixture.Cache->get_stats().Reloads == 2; }) && fixture.Cache->get(ref) != nullptr, "and it arrives");
			});

		register_render_check("environment_budget", [](RenderCheckContext& context)
			{
				EnvironmentFixture fixture(context, 1000);
				const AssetRef first = fixture.Cache->add_inline_environment(make_environment(glm::vec3(1.0f), glm::vec3(0.0f)));
				const AssetRef second = fixture.Cache->add_inline_environment(make_environment(glm::vec3(2.0f), glm::vec3(0.0f)));
				fixture.Cache->get(first);
				fixture.Cache->get(second);
				fixture.frame();
				context.check(fixture.Cache->get_stats().Environments == 1, "only one fits in the first frame");
				u32 frames = 1;
				while (fixture.Cache->get_stats().Environments < 2 && frames < 30)
				{
					fixture.frame();
					frames++;
				}
				context.check(fixture.Cache->get_stats().Environments == 2 && frames >= 2, "the other follows in a later frame");
			});
	}
}
