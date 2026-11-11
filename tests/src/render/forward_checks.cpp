#include <pch.h>
#include "scene_tester.h"

#include <glm/gtc/type_ptr.hpp>

#include <renderer/scene/frustum_culler.h>

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

		register_render_check("frustum_culling_property", [](RenderCheckContext& context)
			{
				// Whatever the camera and the box: if any point of the box is inside the view volume, the box must not be culled.
				u32 seed = 4242;
				auto random = [&seed](float low, float high) { seed = seed * 1664525u + 1013904223u; return low + (high - low) * (static_cast<float>(seed >> 8) / static_cast<float>(1 << 24)); };
				u32 wrong = 0;
				for (int trial = 0; trial < 3000 && wrong == 0; trial++)
				{
					const glm::vec3 eye(random(-20, 20), random(-20, 20), random(-20, 20));
					const glm::vec3 target(random(-5, 5), random(-5, 5), random(-5, 5));
					if (glm::length(target - eye) < 1.0f || std::abs(glm::normalize(target - eye).y) > 0.98f)
					{
						continue;
					}
					const glm::mat4 projection = trial % 2 ? glm::perspectiveRH_ZO(glm::radians(random(20, 120)), random(0.5f, 2.5f), 0.1f, random(20, 200))
						: glm::orthoRH_ZO(-random(2, 20), random(2, 20), -random(2, 20), random(2, 20), 0.1f, random(20, 100));
					const glm::mat4 viewProjection = projection * glm::lookAt(eye, target, glm::vec3(0, 1, 0));
					const glm::mat4 world = glm::translate(glm::mat4(1.0f), glm::vec3(random(-30, 30), random(-30, 30), random(-30, 30)))
						* glm::mat4_cast(glm::angleAxis(random(0, 6.28f), glm::normalize(glm::vec3(random(0.1f, 1), random(0.1f, 1), random(0.1f, 1)))))
						* glm::scale(glm::mat4(1.0f), glm::vec3(random(0.2f, 6), random(0.2f, 6), random(0.2f, 6)));
					const glm::vec3 low(-1.0f);
					const glm::vec3 high(1.0f);
					bool anyInside = false;
					for (int i = 0; i < 125 && !anyInside; i++)
					{
						const glm::vec3 local = glm::mix(low, high, glm::vec3(static_cast<float>(i % 5), static_cast<float>((i / 5) % 5), static_cast<float>(i / 25)) / 4.0f);
						const glm::vec4 clip = viewProjection * world * glm::vec4(local, 1.0f);
						anyInside = clip.w > 0.0f && std::abs(clip.x) <= clip.w && std::abs(clip.y) <= clip.w && clip.z >= 0.0f && clip.z <= clip.w;
					}
					if (anyInside && !FrustumCuller::is_visible(viewProjection, world, low, high))
					{
						wrong++;
					}
				}
				context.check(wrong == 0, "a box with a point inside the view is never culled");
			});

		register_render_check("culling_does_not_change_image", [](RenderCheckContext& context)
			{
				// Random cubes and cameras, with shadows: the culled frame must equal the frame drawn without any culling.
				u32 seed = 99;
				auto random = [&seed](float low, float high) { seed = seed * 1664525u + 1013904223u; return low + (high - low) * (static_cast<float>(seed >> 8) / static_cast<float>(1 << 24)); };
				u32 differing = 0;
				for (int trial = 0; trial < 12; trial++)
				{
					SceneTester test(context);
					const AssetRef cube = test.add_mesh(make_test_cube(test.add_material(test.color_material(glm::vec3(random(0.2f, 1), random(0.2f, 1), random(0.2f, 1))))));
					for (int i = 0; i < 40; i++)
					{
						test.add_instance(cube, glm::translate(glm::mat4(1.0f), glm::vec3(random(-14, 14), random(-3, 8), random(-14, 14)))
							* glm::mat4_cast(glm::angleAxis(random(0, 6.28f), glm::normalize(glm::vec3(random(0.1f, 1), random(0.1f, 1), random(0.1f, 1)))))
							* glm::scale(glm::mat4(1.0f), glm::vec3(random(0.3f, 2.5f))));
					}
					test.add_light(test.sun(glm::normalize(glm::vec3(random(-1, 1), -1.0f, random(-1, 1))), 2.0f, true));
					for (int l = 0; l < 30; l++)
					{
						RenderLight light;
						light.Kind = l % 3 == 0 ? LightKind::Spot : LightKind::Point;
						light.Position = glm::vec3(random(-14, 14), random(-2, 8), random(-14, 14));
						light.Direction = glm::normalize(glm::vec3(random(-1, 1), random(-1, 1), random(-1, 1)));
						light.Range = random(1.0f, 9.0f);
						light.Intensity = random(0.5f, 4.0f);
						light.Color = glm::vec3(random(0.2f, 1), random(0.2f, 1), random(0.2f, 1));
						light.CastsShadows = l % 6 == 0;
						test.add_light(light);
					}
					test.set_camera(glm::vec3(random(-18, 18), random(0, 12), random(-18, 18)), glm::vec3(random(-5, 5), random(0, 3), random(-5, 5)), random(30, 100), 0.1f, 80.0f);

					test.renderer().get_scene_data().set_culling_enabled(true);
					context.check(test.render(), "render culled");
					const std::vector<u8> culled = test.pixels();
					test.renderer().get_scene_data().set_culling_enabled(false);
					context.check(test.render(), "render unculled");
					test.renderer().get_scene_data().set_culling_enabled(true);
					const int difference = SceneTester::max_difference(culled, test.pixels());
					if (difference != 0)
					{
						differing++;
						WI_LOG(Info, "RenderTest", "culling trial {0} differs by {1}", trial, difference);
					}
				}
				context.check(differing == 0, "culling never changes what is drawn");
			});

		register_render_check("culling_real_model", [](RenderCheckContext& context)
			{
				// The sample scene's model (when it is in the asset catalog): culled and unculled frames from many cameras must be equal.
				AssetManager* assets = context.World.get_renderer().get_assets();
				std::optional<AssetRef> model = assets->find_asset("Assets/models/torus_knot.glb");
				if (!model)
				{
					WI_LOG(Info, "RenderTest", "culling_real_model: no Assets/models/torus_knot.glb, skipped");
					return;
				}
				Handle<PrefabData> prefab = assets->load<PrefabData>(*model);
				for (int i = 0; i < 2000 && prefab.state() == LoadState::Loading; i++)
				{
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
				const PrefabData* data = prefab.get();
				context.check(data != nullptr, "the model loads");
				if (data == nullptr)
				{
					return;
				}

				for (const PrefabNode& node : data->Nodes)
				{
					if (!node.Mesh.valid())
					{
						continue;
					}
					Handle<MeshData> mesh = assets->load<MeshData>(node.Mesh);
					for (int i = 0; i < 2000 && mesh.state() == LoadState::Loading; i++)
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(1));
					}
					if (const MeshData* m = mesh.get())
					{
						u32 agree = 0;
						u32 disagree = 0;
						for (size_t t = 0; t + 2 < m->Indices.size(); t += 3)
						{
							auto position = [&](u32 index) { return glm::make_vec3(m->Vertices[index].Position); };
							const glm::vec3 faceNormal = glm::cross(position(m->Indices[t + 1]) - position(m->Indices[t]), position(m->Indices[t + 2]) - position(m->Indices[t]));
							const glm::vec3 vertexNormal = glm::make_vec3(m->Vertices[m->Indices[t]].Normal);
							(glm::dot(faceNormal, vertexNormal) >= 0.0f ? agree : disagree)++;
						}
						const glm::mat4 local = glm::translate(glm::mat4(1.0f), node.Translation) * glm::mat4_cast(node.Rotation) * glm::scale(glm::mat4(1.0f), node.Scale);
						WI_LOG(Info, "RenderTest", "winding of {0}: {1} triangles agree with their normals, {2} do not; node determinant {3}", node.Name, agree, disagree, glm::determinant(local));
						context.check(disagree == 0, "the imported triangles are counter-clockwise seen from outside, as the pipelines expect");
					}
				}

				u32 seed = 5;
				auto random = [&seed](float low, float high) { seed = seed * 1664525u + 1013904223u; return low + (high - low) * (static_cast<float>(seed >> 8) / static_cast<float>(1 << 24)); };
				u32 differing = 0;
				for (int trial = 0; trial < 16; trial++)
				{
					SceneTester test(context);
					for (const PrefabNode& node : data->Nodes)
					{
						if (node.Mesh.valid())
						{
							test.add_instance(node.Mesh, glm::translate(glm::mat4(1.0f), node.Translation) * glm::mat4_cast(node.Rotation) * glm::scale(glm::mat4(1.0f), node.Scale));
						}
					}
					const float radius = random(2.0f, 14.0f);
					const glm::vec3 around(random(-1, 1), random(-1, 1), random(-1, 1));
					test.set_camera(glm::normalize(around + glm::vec3(0.01f)) * radius, glm::vec3(random(-1.5f, 1.5f), random(-1.5f, 1.5f), random(-1.5f, 1.5f)), random(25, 100), 0.1f, 200.0f);
					test.add_light(test.sun(glm::normalize(glm::vec3(random(-1, 1), -1.0f, random(-1, 1))), 2.0f, true));

					test.renderer().get_scene_data().set_culling_enabled(true);
					context.check(test.render(12), "render culled");
					const std::vector<u8> culled = test.pixels();
					test.renderer().get_scene_data().set_culling_enabled(false);
					context.check(test.render(2), "render unculled");
					test.renderer().get_scene_data().set_culling_enabled(true);
					const int difference = SceneTester::max_difference(culled, test.pixels());
					if (difference != 0)
					{
						differing++;
						WI_LOG(Info, "RenderTest", "real model trial {0} differs by {1}", trial, difference);
					}
				}
				context.check(differing == 0, "culling never changes what is drawn of the real model");
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
