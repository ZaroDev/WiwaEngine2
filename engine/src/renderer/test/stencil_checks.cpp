#include <pch.h>
#include "render_test.h"

#include <chrono>
#include <thread>

#include "../engine_assets.h"
#include "../shader_library.h"
#include "scene_tester.h"

// Checks that the viewport depth has a stencil plane that can be written by a draw and sampled by a shader, on every API.
namespace wiwa
{
	void register_stencil_checks()
	{
		register_render_check("viewport_depth_stencil", [](RenderCheckContext& context)
			{
				SceneTester test(context, 32);
				context.check(test.render(), "render an empty scene");
				ViewportTarget& viewport = test.renderer().get_viewport();
				context.check(viewport.depth() && viewport.depth()->getDesc().format == nvrhi::Format::D32S8, "the viewport depth has a stencil plane");
				context.check(viewport.ldr_depth_framebuffer() != nullptr, "the LDR + depth framebuffer exists");
				if (viewport.ldr_depth_framebuffer() != nullptr)
				{
					const nvrhi::FramebufferInfo& info = viewport.ldr_depth_framebuffer()->getFramebufferInfo();
					context.check(info.colorFormats.size() == 1 && info.colorFormats[0] == viewport.ldr()->getDesc().format && info.depthFormat == nvrhi::Format::D32S8, "it holds the LDR color and the depth/stencil");
				}
				context.check(ViewportTarget::STENCIL_VIEW_FORMAT == nvrhi::Format::X32G8_UINT, "the stencil view format");
				test.set_size(48);
				context.check(test.render(), "render after a resize");
				context.check(viewport.ldr_depth_framebuffer() != nullptr && viewport.ldr_depth_framebuffer()->getFramebufferInfo().width == 48, "the framebuffer follows the viewport size");
			});

		register_render_check("stencil_roundtrip", [](RenderCheckContext& context)
			{
				constexpr u32 SIZE = 64;
				SceneTester test(context, SIZE);
				context.check(test.render(), "render an empty scene");

				Renderer& renderer = test.renderer();
				nvrhi::IDevice* device = context.Device;
				ShaderLibrary* shaders = renderer.get_shader_library();
				shaders->request(engine_assets::FXAA_SHADER, ShaderStage::Vertex); // its vertex shader is a fullscreen triangle
				shaders->request(engine_assets::STENCIL_PROBE_SHADER, ShaderStage::Vertex);
				shaders->request(engine_assets::STENCIL_PROBE_SHADER, ShaderStage::Pixel);
				bool ready = false;
				for (int i = 0; i < 1000 && !ready; i++)
				{
					shaders->update();
					ready = shaders->get(engine_assets::FXAA_SHADER, ShaderStage::Vertex) && shaders->get(engine_assets::STENCIL_PROBE_SHADER, ShaderStage::Vertex)
						&& shaders->get(engine_assets::STENCIL_PROBE_SHADER, ShaderStage::Pixel);
					if (!ready)
					{
						std::this_thread::sleep_for(std::chrono::milliseconds(2));
					}
				}
				context.check(ready, "the shaders load");
				if (!ready)
				{
					return;
				}

				ViewportTarget& viewport = renderer.get_viewport();

				// Writes stencil 1 where it draws (no color, no depth).
				nvrhi::GraphicsPipelineDesc writer;
				writer.primType = nvrhi::PrimitiveType::TriangleList;
				writer.VS = shaders->get(engine_assets::FXAA_SHADER, ShaderStage::Vertex);
				writer.renderState.depthStencilState.depthTestEnable = false;
				writer.renderState.depthStencilState.depthWriteEnable = false;
				writer.renderState.depthStencilState.stencilEnable = true;
				writer.renderState.depthStencilState.stencilReadMask = 0xFF;
				writer.renderState.depthStencilState.stencilWriteMask = 0xFF;
				writer.renderState.depthStencilState.stencilRefValue = 1;
				writer.renderState.depthStencilState.frontFaceStencil.stencilFunc = nvrhi::ComparisonFunc::Always;
				writer.renderState.depthStencilState.frontFaceStencil.passOp = nvrhi::StencilOp::Replace;
				writer.renderState.depthStencilState.backFaceStencil = writer.renderState.depthStencilState.frontFaceStencil;
				writer.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
				writer.renderState.rasterState.scissorEnable = true;
				nvrhi::GraphicsPipelineHandle writerPipeline = device->createGraphicsPipeline(writer, viewport.depth_framebuffer());

				// Reads the stencil plane into an R8 target.
				nvrhi::TextureDesc targetDescription;
				targetDescription.width = SIZE;
				targetDescription.height = SIZE;
				targetDescription.format = nvrhi::Format::R8_UNORM;
				targetDescription.isRenderTarget = true;
				targetDescription.initialState = nvrhi::ResourceStates::RenderTarget;
				targetDescription.keepInitialState = true;
				targetDescription.debugName = "stencil probe target";
				nvrhi::TextureHandle target = device->createTexture(targetDescription);
				nvrhi::FramebufferDesc framebufferDescription;
				framebufferDescription.addColorAttachment(target);
				nvrhi::FramebufferHandle framebuffer = device->createFramebuffer(framebufferDescription);

				nvrhi::VulkanBindingOffsets offsets;
				offsets.shaderResource = 0;
				offsets.sampler = 0;
				offsets.constantBuffer = 0;
				offsets.unorderedAccess = 0;
				nvrhi::BindingLayoutDesc layoutDescription;
				layoutDescription.visibility = nvrhi::ShaderType::Pixel;
				layoutDescription.bindingOffsets = offsets;
				layoutDescription.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0));
				nvrhi::BindingLayoutHandle layout = device->createBindingLayout(layoutDescription);

				nvrhi::GraphicsPipelineDesc probe;
				probe.primType = nvrhi::PrimitiveType::TriangleList;
				probe.VS = shaders->get(engine_assets::STENCIL_PROBE_SHADER, ShaderStage::Vertex);
				probe.PS = shaders->get(engine_assets::STENCIL_PROBE_SHADER, ShaderStage::Pixel);
				probe.addBindingLayout(layout);
				probe.renderState.depthStencilState.depthTestEnable = false;
				probe.renderState.depthStencilState.depthWriteEnable = false;
				probe.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
				nvrhi::GraphicsPipelineHandle probePipeline = device->createGraphicsPipeline(probe, framebuffer);

				nvrhi::BindingSetDesc setDescription;
				setDescription.addItem(nvrhi::BindingSetItem::Texture_SRV(0, viewport.depth(), nvrhi::Format::X32G8_UINT));
				nvrhi::BindingSetHandle set = device->createBindingSet(setDescription, layout);

				nvrhi::CommandListHandle commandList = device->createCommandList();
				context.check(writerPipeline && target && framebuffer && layout && probePipeline && set && commandList, "the probe objects");
				if (!writerPipeline || !target || !framebuffer || !layout || !probePipeline || !set || !commandList)
				{
					return;
				}

				nvrhi::DrawArguments fullscreen;
				fullscreen.vertexCount = 3;

				commandList->open();
				commandList->clearDepthStencilTexture(viewport.depth(), nvrhi::AllSubresources, false, 0.0f, true, 0);

				nvrhi::GraphicsState write;
				write.pipeline = writerPipeline;
				write.framebuffer = viewport.depth_framebuffer();
				write.viewport.addViewport(nvrhi::Viewport(static_cast<float>(SIZE), static_cast<float>(SIZE)));
				write.viewport.addScissorRect(nvrhi::Rect(0, SIZE / 2, 0, SIZE)); // the left half
				commandList->setGraphicsState(write);
				commandList->draw(fullscreen);

				nvrhi::GraphicsState read;
				read.pipeline = probePipeline;
				read.framebuffer = framebuffer;
				read.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<float>(SIZE), static_cast<float>(SIZE)));
				read.addBindingSet(set);
				commandList->setGraphicsState(read);
				commandList->draw(fullscreen);
				commandList->close();
				device->executeCommandList(commandList);

				std::vector<u8> bytes;
				u32 width = 0;
				u32 height = 0;
				context.check(read_texture_slice_bytes(device, target, 0, 0, bytes, width, height, 1) && width == SIZE && height == SIZE, "read the probe back");
				if (bytes.size() == static_cast<size_t>(SIZE) * SIZE)
				{
					context.check(bytes[32 * SIZE + 8] == 255, "stencil 1 on the left half");
					context.check(bytes[8 * SIZE + 20] == 255, "stencil 1 on the left half, top");
					context.check(bytes[32 * SIZE + 56] == 0, "stencil 0 on the right half");
					context.check(bytes[56 * SIZE + 40] == 0, "stencil 0 on the right half, bottom");
				}
			});
	}
}
