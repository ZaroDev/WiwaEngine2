#include <pch.h>
#include "scene_tester.h"

#include "../passes/blit_pass.h"

// Checks of alpha modes, blending, the resolve and the fullscreen blit.
namespace wiwa
{
	namespace
	{
		SceneSettings dark(glm::vec3 background = glm::vec3(0.0f))
		{
			SceneSettings settings;
			settings.AmbientIntensity = 0.0f;
			settings.BackgroundColor = background;
			return settings;
		}

		MaterialData emissive_material(glm::vec3 emissive)
		{
			MaterialData material;
			material.BaseColorFactor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
			material.EmissiveFactor = emissive;
			return material;
		}

		glm::mat4 at(glm::vec3 position)
		{
			return glm::translate(glm::mat4(1.0f), position);
		}
	}

	void register_blend_checks()
	{
		register_render_check("alpha_mask", [](RenderCheckContext& context)
			{
				SceneTester test(context);

				// A quad whose texture is cut out on the left half, with a green box behind it.
				TextureData texture;
				texture.Width = 2;
				texture.Height = 1;
				texture.Mips.push_back({ 255, 255, 255, 0, 255, 255, 255, 255 });
				texture.Mips.push_back({ 255, 255, 255, 128 });
				MaterialData quadMaterial;
				quadMaterial.BaseColorFactor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
				quadMaterial.Alpha = AlphaMode::Mask;
				quadMaterial.AlphaCutoff = 0.5f;
				quadMaterial.DoubleSided = false; // the back of the box would show through the cut-out, with its texture mirrored
				quadMaterial.BaseColorTexture = test.add_texture(std::move(texture));

				test.add_instance(test.add_mesh(make_test_box(glm::vec3(1.0f, 1.0f, 0.02f), test.add_material(quadMaterial))), glm::mat4(1.0f));
				test.add_instance(test.add_mesh(make_test_box(glm::vec3(1.5f), test.add_material(test.color_material(glm::vec3(0.0f, 1.0f, 0.0f))))), at({ 0.0f, 0.0f, -2.0f }));
				context.check(test.render(), "render");

				const glm::ivec3 left = test.pixel(test.to_pixel(glm::vec3(-0.5f, 0.0f, 0.02f)));
				const glm::ivec3 right = test.pixel(test.to_pixel(glm::vec3(0.5f, 0.0f, 0.02f)));
				WI_LOG(Info, "RenderTest", "alpha_mask left ({0}, {1}, {2}) right ({3}, {4}, {5})", left.r, left.g, left.b, right.r, right.g, right.b);
				context.check(left.g > left.r * 3 && left.g > 20, "the cut-out half shows the green box behind it");
				context.check(right.r > right.g * 3 && right.r > 20, "the other half is the red quad");
			});

		register_render_check("alpha_blend", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.scene().set_settings(dark(glm::vec3(0.0f, 0.0f, 0.25f)));
				context.check(test.render(2), "render the background");
				const glm::ivec3 background = test.pixel(32, 32);

				MaterialData glass;
				glass.BaseColorFactor = glm::vec4(1.0f, 0.0f, 0.0f, 0.5f);
				glass.Alpha = AlphaMode::Blend;
				SceneSettings settings = dark(glm::vec3(0.0f, 0.0f, 0.25f));
				settings.AmbientIntensity = 0.3f;
				test.scene().set_settings(settings);
				test.add_instance(test.add_mesh(make_test_box(glm::vec3(1.0f, 1.0f, 0.02f), test.add_material(glass))), glm::mat4(1.0f));
				context.check(test.render(), "render with the blended quad");

				const glm::ivec3 mixed = test.pixel(32, 32);
				WI_LOG(Info, "RenderTest", "alpha_blend background ({0}, {1}, {2}) mixed ({3}, {4}, {5})", background.r, background.g, background.b, mixed.r, mixed.g, mixed.b);
				context.check(mixed.r > 40 && mixed.b > 60 && mixed.r < 200 && mixed.b < 200, "the quad lets the blue background through and adds red");
			});

		register_render_check("blend_sorting", [](RenderCheckContext& context)
			{
				auto render_pair = [&](bool redFirst, std::vector<u8>& pixels)
				{
					SceneTester test(context);
					MaterialData red;
					red.BaseColorFactor = glm::vec4(1.0f, 0.0f, 0.0f, 0.5f);
					red.Alpha = AlphaMode::Blend;
					MaterialData green;
					green.BaseColorFactor = glm::vec4(0.0f, 1.0f, 0.0f, 0.5f);
					green.Alpha = AlphaMode::Blend;
					const AssetRef redMesh = test.add_mesh(make_test_box(glm::vec3(0.9f, 0.9f, 0.02f), test.add_material(red)));
					const AssetRef greenMesh = test.add_mesh(make_test_box(glm::vec3(0.9f, 0.9f, 0.02f), test.add_material(green)));
					const glm::mat4 front = at({ -0.4f, 0.0f, 0.5f });
					const glm::mat4 back = at({ 0.4f, 0.0f, -0.5f });
					if (redFirst)
					{
						test.add_instance(redMesh, front);
						test.add_instance(greenMesh, back);
					}
					else
					{
						test.add_instance(greenMesh, back);
						test.add_instance(redMesh, front);
					}
					const bool ok = test.render();
					pixels = test.pixels();
					return ok;
				};

				std::vector<u8> first;
				std::vector<u8> second;
				context.check(render_pair(true, first) && render_pair(false, second), "render both orders");
				context.check(SceneTester::max_difference(first, second) == 0, "the order the quads were added in does not change the image");
			});

		register_render_check("resolve_tonemap", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.scene().set_settings(dark());
				const AssetRef cube = test.add_mesh(make_test_cube(test.add_material(emissive_material(glm::vec3(4.0f, 0.0f, 0.0f)))));
				const EntityGuid bright = test.add_instance(cube, glm::mat4(1.0f));
				context.check(test.render(), "render a bright emissive cube");
				const glm::ivec3 hot = test.pixel(32, 32);
				WI_LOG(Info, "RenderTest", "resolve_tonemap bright ({0}, {1}, {2})", hot.r, hot.g, hot.b);
				context.check(hot.r > 200 && hot.r < 255 && hot.g < 20 && hot.b < 20, "a value of 4 stays red and is not clipped to white");

				test.remove_instance(bright);
				test.add_instance(test.add_mesh(make_test_cube(test.add_material(emissive_material(glm::vec3(0.0f))))), glm::mat4(1.0f));
				context.check(test.render(), "render a black cube");
				const glm::ivec3 black = test.pixel(32, 32);
				context.check(black.r == 0 && black.g == 0 && black.b == 0, "black stays black");
			});

		register_render_check("exposure_scales", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.add_instance(test.add_mesh(make_test_cube(test.add_material(emissive_material(glm::vec3(0.2f))))), glm::mat4(1.0f));

				int luma[3]{};
				const float exposures[3] = { 0.5f, 1.0f, 2.0f };
				for (int i = 0; i < 3; i++)
				{
					SceneSettings settings = dark();
					settings.ExposureEv = std::log2(exposures[i]);
					test.scene().set_settings(settings);
					context.check(test.render(), "render");
					luma[i] = SceneTester::luma(test.pixel(32, 32));
				}
				WI_LOG(Info, "RenderTest", "exposure_scales {0} {1} {2}", luma[0], luma[1], luma[2]);
				context.check(luma[0] < luma[1] && luma[1] < luma[2], "more exposure, brighter image");
			});

		register_render_check("blit_fullscreen", [](RenderCheckContext& context)
			{
				SceneTester test(context);
				test.add_instance(test.add_mesh(make_test_cube(test.add_material(test.color_material(glm::vec3(1.0f, 0.0f, 0.0f))))), glm::mat4(1.0f));
				context.check(test.render(), "render the scene");
				const glm::ivec3 scene = test.pixel(32, 32);

				Renderer& renderer = test.renderer();
				nvrhi::IDevice* device = context.Device;
				TextureRegistry registry;
				BlitPass pass;
				PassInitializationDescription description;
				description.Device = device;
				description.Backbuffer = renderer.get_reference_framebuffer();
				description.Shaders = renderer.get_shader_library();
				description.Textures = &registry;
				context.check(pass.initialize(description), "the pass initializes");
				bool ready = false;
				for (int i = 0; i < 500 && !ready; i++)
				{
					renderer.get_shader_library()->update();
					pass.update(device, renderer.get_reference_framebuffer());
					ready = pass.is_ready();
					if (!ready)
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(2));
					}
				}
				context.check(ready, "the pass becomes ready");
				if (!ready)
				{
					return;
				}

				nvrhi::TextureDesc targetDescription;
				targetDescription.width = 64;
				targetDescription.height = 64;
				targetDescription.format = nvrhi::Format::BGRA8_UNORM;
				targetDescription.isRenderTarget = true;
				targetDescription.initialState = nvrhi::ResourceStates::RenderTarget;
				targetDescription.keepInitialState = true;
				targetDescription.debugName = "blit test target";
				nvrhi::TextureHandle target = device->createTexture(targetDescription);
				nvrhi::FramebufferDesc framebufferDescription;
				framebufferDescription.addColorAttachment(target);
				nvrhi::FramebufferHandle framebuffer = device->createFramebuffer(framebufferDescription);
				nvrhi::CommandListHandle commandList = device->createCommandList();
				context.check(target && framebuffer && commandList, "the test target");
				if (!target || !framebuffer || !commandList)
				{
					return;
				}

				// Draws the pass over a green target and reads the middle pixel back (as B, G, R, A).
				auto draw = [&](bool sceneInViewport, u8* pixel) -> bool
				{
					FrameDescription frame;
					frame.Backbuffer = framebuffer;
					frame.Width = 64;
					frame.Height = 64;
					frame.Scene = &renderer.get_scene_data();
					frame.SceneInViewport = sceneInViewport;
					commandList->open();
					commandList->clearTextureFloat(target, nvrhi::AllSubresources, nvrhi::Color(0.0f, 1.0f, 0.0f, 1.0f));
					pass.render(commandList, frame);
					commandList->close();
					device->executeCommandList(commandList);
					std::vector<u8> pixels;
					u32 width = 0;
					u32 height = 0;
					if (!read_texture_rgba8(device, target, pixels, width, height))
					{
						return false;
					}
					std::memcpy(pixel, &pixels[(32 * 64 + 32) * 4], 4);
					return true;
				};

				u8 pixel[4]{};
				context.check(draw(false, pixel), "draw fullscreen");
				context.check(std::abs(pixel[2] - scene.r) <= 2 && std::abs(pixel[1] - scene.g) <= 2 && std::abs(pixel[0] - scene.b) <= 2, "the window shows the scene image");
				context.check(draw(true, pixel), "draw with the scene in a panel");
				context.check(pixel[1] > 250 && pixel[2] < 5, "nothing is drawn then");
			});
	}
}
