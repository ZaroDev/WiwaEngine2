#include <pch.h>
#include "render_test.h"

#include "../passes/imgui_pass.h"
#include "../render_world.h"
#include "../renderer.h"

// Checks of the viewport target, the texture registry and drawing the viewport through the UI pass.
namespace wiwa
{
	namespace
	{
		bool near_color(const u8* pixel, int r, int g, int b, int tolerance)
		{
			return std::abs(pixel[0] - r) <= tolerance && std::abs(pixel[1] - g) <= tolerance && std::abs(pixel[2] - b) <= tolerance;
		}

		void clear_texture(nvrhi::IDevice* device, nvrhi::ITexture* texture, nvrhi::Color color)
		{
			nvrhi::CommandListHandle commandList = device->createCommandList();
			commandList->open();
			commandList->clearTextureFloat(texture, nvrhi::AllSubresources, color);
			commandList->close();
			device->executeCommandList(commandList);
		}

		// A quad over the whole 64x64 target, one draw command that uses the texture id.
		OwnedDrawData make_quad(u32 textureId)
		{
			OwnedDrawData data;
			data.Valid = true;
			data.DisplaySize[0] = 64.0f;
			data.DisplaySize[1] = 64.0f;

			const float corners[4][4] = { { 0, 0, 0, 0 }, { 64, 0, 1, 0 }, { 64, 64, 1, 1 }, { 0, 64, 0, 1 } };
			for (const auto& corner : corners)
			{
				ImDrawVert vertex{};
				vertex.pos = ImVec2(corner[0], corner[1]);
				vertex.uv = ImVec2(corner[2], corner[3]);
				vertex.col = 0xFFFFFFFFu;
				data.Vertices.push_back(vertex);
			}
			for (ImDrawIdx index : { 0, 1, 2, 0, 2, 3 })
			{
				data.Indices.push_back(index);
			}

			OwnedDrawCommand command;
			command.ClipRect[2] = 64.0f;
			command.ClipRect[3] = 64.0f;
			command.ElementCount = 6;
			command.TextureId = textureId;
			data.Commands.push_back(command);
			return data;
		}
	}

	void register_viewport_checks()
	{
		register_render_check("viewport_target_create", [](RenderCheckContext& context)
			{
				ViewportTarget target;
				context.check(target.create(context.Device, 64, 48), "create");
				context.check(target.hdr() && target.depth() && target.ldr(), "all three textures");
				context.check(target.hdr_framebuffer() && target.depth_framebuffer() && target.ldr_framebuffer(), "all three framebuffers");
				if (target.hdr() == nullptr || target.ldr() == nullptr || target.depth() == nullptr)
				{
					return;
				}
				context.check(target.width() == 64 && target.height() == 48, "size");
				context.check(target.hdr()->getDesc().format == nvrhi::Format::RGBA16_FLOAT, "HDR is RGBA16F");
				context.check(target.ldr()->getDesc().format == nvrhi::Format::RGBA8_UNORM, "LDR is RGBA8");
				context.check(target.ldr()->getDesc().width == 64 && target.depth()->getDesc().height == 48, "texture sizes");

				ViewportTarget tiny;
				context.check(tiny.create(context.Device, 0, 0) && tiny.width() == 1 && tiny.height() == 1 && tiny.ldr() != nullptr, "0x0 becomes 1x1");
				context.check(ViewportTarget::clamp_dimension(99999) == ViewportTarget::MAX_SIZE, "huge sizes are clamped");
				context.check(ViewportTarget::clamp_dimension(0) == 1 && ViewportTarget::clamp_dimension(777) == 777, "small and ordinary sizes");
			});

		register_render_check("viewport_resize", [](RenderCheckContext& context)
			{
				ViewportTarget target;
				context.check(target.create(context.Device, 64, 48), "create");
				nvrhi::TextureHandle first = target.ldr(); // kept alive, so the next one cannot take its address
				const u32 generation = target.generation();
				context.check(target.create(context.Device, 32, 32), "create again");
				context.check(target.ldr() != first.Get() && target.width() == 32 && target.height() == 32, "replaced at the new size");
				context.check(target.generation() != generation, "the generation moved on");
			});

		register_render_check("registry_roundtrip", [](RenderCheckContext& context)
			{
				nvrhi::TextureDesc description;
				description.width = 4;
				description.height = 4;
				description.format = nvrhi::Format::RGBA8_UNORM;
				description.isShaderResource = true;
				nvrhi::TextureHandle a = context.Device->createTexture(description);
				nvrhi::TextureHandle b = context.Device->createTexture(description);

				TextureRegistry registry;
				const u32 id = registry.add(a);
				context.check(id >= FIRST_FREE_TEXTURE_ID && registry.get(id) == a.Get(), "add and get");
				registry.set(id, b);
				context.check(registry.get(id) == b.Get(), "set replaces");
				context.check(registry.contains(b) && !registry.contains(a), "contains follows");
				registry.set(VIEWPORT_TEXTURE_ID, a);
				context.check(registry.get(VIEWPORT_TEXTURE_ID) == a.Get(), "a fixed id");
				registry.remove(id);
				context.check(registry.get(id) == nullptr, "removed");
				context.check(registry.get(12345) == nullptr, "an unknown id gives null");
				context.check(registry.add(a) != id, "ids are not reused");
			});

		register_render_check("viewport_cleared_pixels", [](RenderCheckContext& context)
			{
				ViewportTarget target;
				context.check(target.create(context.Device, 64, 48), "create");
				clear_texture(context.Device, target.ldr(), nvrhi::Color(0.1f, 0.2f, 0.3f, 1.0f));

				std::vector<u8> pixels;
				u32 width = 0;
				u32 height = 0;
				context.check(read_texture_rgba8(context.Device, target.ldr(), pixels, width, height) && pixels.size() == 64 * 48 * 4, "readback");
				if (pixels.size() == 64 * 48 * 4)
				{
					context.check(near_color(&pixels[(24 * 64 + 32) * 4], 26, 51, 77, 2), "the LDR target holds the clear color");
				}
			});

		register_render_check("imgui_binds_viewport_texture", [](RenderCheckContext& context)
			{
				Renderer& renderer = context.World.get_renderer();
				nvrhi::IDevice* device = context.Device;

				TextureRegistry registry;
				ImGuiRenderPass pass;
				PassInitializationDescription description;
				description.Device = device;
				description.Backbuffer = renderer.get_reference_framebuffer();
				description.Shaders = renderer.get_shader_library();
				description.Textures = &registry;
				context.check(pass.initialize(description), "the pass initializes");

				const u8 white[4] = { 255, 255, 255, 255 };
				pass.set_font_atlas(1, 1, white);
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
				context.check(registry.get(FONT_TEXTURE_ID) != nullptr, "the pass registered its font atlas");

				// The viewport target, red.
				ViewportTarget viewport;
				context.check(viewport.create(device, 64, 64), "viewport target");
				clear_texture(device, viewport.ldr(), nvrhi::Color(1.0f, 0.0f, 0.0f, 1.0f));
				registry.set(VIEWPORT_TEXTURE_ID, viewport.ldr());

				// What the pass draws into: the same format as the backbuffer it was built for.
				nvrhi::TextureDesc targetDescription;
				targetDescription.width = 64;
				targetDescription.height = 64;
				targetDescription.format = nvrhi::Format::BGRA8_UNORM;
				targetDescription.isRenderTarget = true;
				targetDescription.initialState = nvrhi::ResourceStates::RenderTarget;
				targetDescription.keepInitialState = true;
				targetDescription.debugName = "imgui test target";
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

				// Pixels come back as B, G, R, A.
				auto draw_and_read = [&](u32 textureId, u8* pixel) -> bool
				{
					const OwnedDrawData data = make_quad(textureId);
					FrameDescription frame;
					frame.Backbuffer = framebuffer;
					frame.Width = 64;
					frame.Height = 64;
					frame.ImGui = &data;

					commandList->open();
					commandList->clearTextureFloat(target, nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f));
					pass.render(commandList, frame);
					commandList->close();
					device->executeCommandList(commandList);

					std::vector<u8> pixels;
					u32 width = 0;
					u32 height = 0;
					if (!read_texture_rgba8(device, target, pixels, width, height) || pixels.size() != 64 * 64 * 4)
					{
						return false;
					}
					std::memcpy(pixel, &pixels[(32 * 64 + 32) * 4], 4);
					return true;
				};

				u8 pixel[4]{};
				context.check(draw_and_read(VIEWPORT_TEXTURE_ID, pixel), "draw with the viewport texture");
				context.check(pixel[2] > 200 && pixel[1] < 40 && pixel[0] < 40, "the viewport's red shows");
				context.check(draw_and_read(FONT_TEXTURE_ID, pixel), "draw with the font texture");
				context.check(pixel[2] > 200 && pixel[1] > 200 && pixel[0] > 200, "the font's white shows");
				context.check(draw_and_read(999, pixel), "draw with an unknown texture id");
				context.check(pixel[2] < 40 && pixel[1] < 40 && pixel[0] < 40, "nothing is drawn for it");
			});
	}
}
