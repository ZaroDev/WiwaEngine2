#include <pch.h>
#include "scene_tester.h"

#include <chrono>
#include <filesystem>
#include <fstream>

#include <scene/scene.h>

#include "../scene/scene_extractor.h"

// Checks of the whole path under load and under change: 20,000 instances, hot reload, resizing, minimizing, replacing the scene,
// the copy queue and the graphics queue making the same image, and the three APIs making the same image.
namespace wiwa
{
	namespace
	{
		double milliseconds_since(std::chrono::steady_clock::time_point start)
		{
			return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		}

		glm::mat4 at(glm::vec3 position)
		{
			return glm::translate(glm::mat4(1.0f), position);
		}

		// A scene with a lit, shadowed cube on a plane, a point light and a blended quad: what the cross-backend and queue checks draw.
		void build_reference_scene(SceneTester& test)
		{
			test.set_camera(glm::vec3(3.0f, 2.5f, 6.0f), glm::vec3(0.0f, 0.5f, 0.0f));
			const AssetRef grey = test.add_material(test.color_material(glm::vec3(0.7f), 0.8f));
			const AssetRef red = test.add_material(test.color_material(glm::vec3(0.9f, 0.15f, 0.1f), 0.4f));
			MaterialData glass;
			glass.BaseColorFactor = glm::vec4(0.1f, 0.9f, 0.2f, 0.5f);
			glass.Alpha = AlphaMode::Blend;

			test.add_instance(test.add_mesh(make_test_box(glm::vec3(6.0f, 0.05f, 6.0f), grey)), glm::mat4(1.0f));
			test.add_instance(test.add_mesh(make_test_box(glm::vec3(0.5f), red)), at({ 0.0f, 1.0f, 0.0f }));
			test.add_instance(test.add_mesh(make_test_box(glm::vec3(0.8f, 0.8f, 0.02f), test.add_material(glass))), at({ -1.5f, 1.0f, 1.5f }));

			test.add_light(test.sun(glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f)), 2.0f, true));
			RenderLight point;
			point.Kind = LightKind::Point;
			point.Position = glm::vec3(1.5f, 1.0f, 1.5f);
			point.Range = 5.0f;
			point.Intensity = 3.0f;
			point.Color = glm::vec3(0.4f, 0.5f, 1.0f);
			test.add_light(point);
		}

		bool render_reference(RenderCheckContext& context, std::vector<u8>& pixels)
		{
			SceneTester test(context, 96);
			build_reference_scene(test);
			if (!test.render())
			{
				return false;
			}
			pixels = test.pixels();
			return true;
		}
	}

	void register_scale_checks()
	{
		register_render_check("scale_20000", [](RenderCheckContext& context)
			{
				constexpr u32 COUNT = 20000;
				constexpr u32 COLUMNS = 141;

				// The main thread's side: a scene of 20,000 entities and the extraction of what did not change.
				Ref<Scene> scene = CreateRef<Scene>();
				AssetRef meshRef;
				meshRef.Guid = AssetGuid::generate();
				for (u32 i = 0; i < COUNT; i++)
				{
					const entt::entity entity = scene->create("cube");
					scene->registry().emplace<MeshRenderer>(entity, MeshRenderer{ meshRef, {} });
					scene->registry().get<Transform>(entity).Position = glm::vec3(static_cast<float>(i % COLUMNS) * 2.5f, 0.0f, static_cast<float>(i / COLUMNS) * 2.5f);
				}
				scene->update_world_transforms();

				SceneExtractor extractor;
				SceneDelta everything = extractor.extract(scene, 1.0f);
				context.check(everything.InstanceUpserts.size() == COUNT, "the first extraction has every instance");
				extractor.extract(scene, 1.0f);
				const auto extractStart = std::chrono::steady_clock::now();
				const SceneDelta unchanged = extractor.extract(scene, 1.0f);
				const double extractMs = milliseconds_since(extractStart);
				context.check(unchanged.empty(), "an unchanged scene extracts to nothing");

				// The render thread's side: the same instances drawn.
				SceneTester test(context, 128);
				const AssetRef material = test.add_material(test.color_material(glm::vec3(0.6f)));
				const AssetRef cube = test.add_mesh(make_test_cube(material));
				SceneDelta delta;
				for (auto& [guid, instance] : everything.InstanceUpserts)
				{
					instance.Mesh = cube;
					delta.InstanceUpserts.emplace_back(guid, std::move(instance));
				}
				test.scene().apply(std::move(delta));
				test.set_camera(glm::vec3(176.0f, 240.0f, 360.0f), glm::vec3(176.0f, 0.0f, 176.0f), 60.0f, 0.5f, 1500.0f);
				test.add_light(test.sun(glm::normalize(glm::vec3(-0.4f, -1.0f, -0.3f)), 2.0f, true));

				const u32 errorsBefore = get_error_message_count();
				context.check(test.render(8), "render 20,000 cubes");
				context.check(get_error_message_count() == errorsBefore, "no GPU error");

				// The culling and list building of a frame (the scene did not change: the steady state).
				SceneRenderData& data = test.renderer().get_scene_data();
				double buildMs = 1e9;
				for (int i = 0; i < 5; i++)
				{
					const auto start = std::chrono::steady_clock::now();
					data.build(test.scene(), test.cache(), test.renderer().get_viewport());
					buildMs = std::min(buildMs, milliseconds_since(start));
				}
				
				
				const auto frameBegin = std::chrono::steady_clock::now();
				context.check(test.frame(), "one more frame");
				const double frameMs = milliseconds_since(frameBegin);

				WI_LOG(Info, "RenderTest", "scale_20000: extract (unchanged) {0:.3f} ms, culling and lists {1:.3f} ms ({2} draws visible), whole frame {3:.2f} ms",
					extractMs, buildMs, data.opaque().size() + data.masked().size() + data.blended().size(), frameMs);
				context.check(!data.opaque().empty(), "something is visible");
#ifndef WI_DEBUG // a Debug build is many times slower: there the numbers are only logged
				context.check(extractMs < 1.5, "extracting an unchanged scene takes under 1.5 ms");
				context.check(buildMs < 8.0, "culling and building the draw lists take under 8 ms");
#endif
			});

		register_render_check("cross_backend", [](RenderCheckContext& context)
			{
				std::vector<u8> pixels;
				context.check(render_reference(context, pixels), "render the reference scene");
				if (pixels.empty())
				{
					return;
				}

				namespace fs = std::filesystem;
				const std::string own = std::string(graphics_api_name(context.Api));
				std::error_code error;
				fs::create_directories("Library", error);
				{
					std::ofstream file("Library/render_test_" + own + ".rgba", std::ios::binary | std::ios::trunc);
					file.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
				}

				u32 compared = 0;
				for (const char* other : { "vulkan", "d3d11", "d3d12" })
				{
					if (own == other)
					{
						continue;
					}
					std::ifstream file(std::string("Library/render_test_") + other + ".rgba", std::ios::binary);
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
					WI_LOG(Info, "RenderTest", "cross_backend: {0} against {1}: mean difference {2:.3f}, largest {3}", own, other, mean, worst);
					context.check(mean < 3.0 && worst <= 40, std::string("the image matches ") + other);
					compared++;
				}
				if (compared == 0)
				{
					WI_LOG(Info, "RenderTest", "cross-backend comparison needs the other APIs' output (run --render-test on each API)");
				}
			});

		register_render_check("hot_reload_mesh_texture", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.scene().set_settings([] { SceneSettings settings; settings.AmbientIntensity = 1.0f; return settings; }());

				auto solid = [](u8 r, u8 g, u8 b)
				{
					TextureData texture;
					texture.Width = 2;
					texture.Height = 2;
					texture.Mips.push_back(std::vector<u8>{ r, g, b, 255, r, g, b, 255, r, g, b, 255, r, g, b, 255 });
					texture.Mips.push_back(std::vector<u8>{ r, g, b, 255 });
					return texture;
				};

				MaterialData material;
				material.BaseColorTexture = test.add_texture(solid(255, 0, 0));
				const AssetRef materialRef = test.add_material(material);
				const AssetRef mesh = test.add_mesh(make_test_cube(materialRef));
				test.add_instance(mesh, glm::mat4(1.0f));
				context.check(test.render(), "render the red cube");
				const glm::ivec3 before = test.pixel(32, 32);
				context.check(before.r > 3 * before.b && before.r > 20, "the cube is red");

				// A texture cooked again: the new pixels show on the next frames.
				test.cache().set_inline_texture(material.BaseColorTexture, solid(0, 0, 255));
				test.cache().invalidate(material.BaseColorTexture.resolved());
				context.check(test.render(), "render after the texture changed");
				const glm::ivec3 after = test.pixel(32, 32);
				context.check(after.b > 3 * after.r && after.b > 20, "the cube is blue now");

				// A mesh cooked again: the cube is smaller and leaves what was its edge empty.
				const glm::ivec2 edge = test.to_pixel(glm::vec3(0.8f, 0.0f, 1.0f));
				const glm::ivec3 edgeBefore = test.pixel(edge);
				test.cache().set_inline_mesh(mesh, make_test_box(glm::vec3(0.4f), materialRef));
				test.cache().invalidate(mesh.resolved());
				context.check(test.render(), "render after the mesh changed");
				const glm::ivec3 edgeAfter = test.pixel(edge);
				WI_LOG(Info, "RenderTest", "hot_reload: edge before ({0}, {1}, {2}) after ({3}, {4}, {5})", edgeBefore.r, edgeBefore.g, edgeBefore.b, edgeAfter.r, edgeAfter.g, edgeAfter.b);
				context.check(edgeBefore.b > 20 && edgeAfter.b < edgeBefore.b / 2, "the smaller mesh no longer covers the old edge");
				context.check(test.cache().get_stats().Reloads >= 2, "the cache counted the reloads");
			});

		register_render_check("viewport_resize_frames", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				build_reference_scene(test);
				const u32 errorsBefore = get_error_message_count();
				for (const u32 size : { 64u, 128u, 1u, 7u, 64u })
				{
					test.set_size(size);
					context.check(test.render(2), "render at the new size");
					context.check(test.renderer().get_viewport().width() == size, "the target has the new size");
				}
				context.check(get_error_message_count() == errorsBefore, "no GPU error while resizing");
				context.check(test.pixels().size() == 64 * 64 * 4 && SceneTester::luma(test.pixel(32, 32)) > 0, "the last image is a normal one");
			});

		register_render_check("minimize_resume", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				build_reference_scene(test);
				context.check(test.render(), "render");
				const std::vector<u8> before = test.pixels();

				RenderBatch minimized;
				minimized.Minimized = true;
				minimized.Width = test.renderer().get_width();
				minimized.Height = test.renderer().get_height();
				context.check(test.renderer().render_frame(minimized), "a minimized batch is accepted");
				RenderBatch empty;
				context.check(test.renderer().render_frame(empty), "so is a batch of size 0");

				context.check(test.render(), "a normal batch renders again");
				context.check(SceneTester::max_difference(test.pixels(), before) == 0, "and gives the same image");
			});

		register_render_check("scene_swap", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.scene().set_settings(SceneSettings{});
				const AssetRef red = test.add_material(test.color_material(glm::vec3(1.0f, 0.0f, 0.0f)));
				const AssetRef green = test.add_material(test.color_material(glm::vec3(0.0f, 1.0f, 0.0f)));
				test.add_instance(test.add_mesh(make_test_cube(red)), glm::mat4(1.0f));
				context.check(test.frame(), "a frame with the old scene, its assets still loading");

				// The scene is replaced while the old one's uploads are still pending.
				SceneDelta reset;
				reset.Reset = true;
				test.scene().apply(std::move(reset));
				RenderInstance instance;
				instance.World = at({ 0.0f, 0.0f, 0.0f });
				instance.Mesh = test.add_mesh(make_test_cube(green));
				SceneDelta replacement;
				replacement.InstanceUpserts.emplace_back(EntityGuid{ 99, 99 }, std::move(instance));
				test.scene().apply(std::move(replacement));
				context.check(test.render(), "render the new scene");

				const glm::ivec3 center = test.pixel(32, 32);
				context.check(center.g > 3 * center.r && center.g > 20, "only the new scene's cube is drawn");
				context.check(test.scene().instance_count() == 1, "the old instance is gone");
			});

		register_render_check("copy_queue_fallback_same_image", [](RenderCheckContext& context)
			{
				Renderer& renderer = context.World.get_renderer();
				std::vector<u8> withCopyQueue;
				context.check(render_reference(context, withCopyQueue), "render with the default upload queue");

				renderer.get_upload_queue().force_graphics_queue(true);
				std::vector<u8> onGraphics;
				const bool rendered = render_reference(context, onGraphics);
				renderer.get_upload_queue().force_graphics_queue(false);
				context.check(rendered, "render with uploads on the graphics queue");
				WI_LOG(Info, "RenderTest", "copy queue {0}: uploads on the copy queue and on the graphics queue differ by {1}", context.HasCopyQueue ? "present" : "absent",
					SceneTester::max_difference(withCopyQueue, onGraphics));
				context.check(SceneTester::max_difference(withCopyQueue, onGraphics) == 0, "the image is the same either way");
			});
	}
}
