#include <pch.h>
#include "pick_pass.h"

#include "../engine_assets.h"
#include "../scene/scene_render_data.h"

#define WI_PICK_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	bool PickPass::initialize(const PassInitializationDescription& description)
	{
		m_Device = description.Device;
		m_Shaders = description.Shaders;
		if (m_Shaders == nullptr)
		{
			WI_PICK_LOG(Error, "The pick pass needs the shader library");
			return false;
		}

		for (const char* path : { engine_assets::PICK_SHADER, engine_assets::PICK_ICON_SHADER })
		{
			m_Shaders->request(path, ShaderStage::Vertex);
			m_Shaders->request(path, ShaderStage::Pixel);
		}

		// Slots match engine/shaders/pick_icon.hlsl: SRV 0, push constants 1.
		nvrhi::VulkanBindingOffsets offsets;
		offsets.shaderResource = 0;
		offsets.sampler = 0;
		offsets.constantBuffer = 0;
		offsets.unorderedAccess = 0;
		nvrhi::BindingLayoutDesc layout;
		layout.visibility = nvrhi::ShaderType::All;
		layout.bindingOffsets = offsets;
		layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0));
		layout.addItem(nvrhi::BindingLayoutItem::PushConstants(1, sizeof(IconConstants)));
		m_IconLayout = m_Device->createBindingLayout(layout);
		return m_IconLayout != nullptr;
	}

	bool PickPass::is_ready() const
	{
		for (const char* path : { engine_assets::PICK_SHADER, engine_assets::PICK_ICON_SHADER })
		{
			if (m_Shaders->get(path, ShaderStage::Vertex) == nullptr || m_Shaders->get(path, ShaderStage::Pixel) == nullptr)
			{
				return false;
			}
		}
		return true;
	}

	void PickPass::update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer)
	{
		for (const char* path : { engine_assets::PICK_SHADER, engine_assets::PICK_ICON_SHADER })
		{
			if (m_Shaders->has_failed(path, ShaderStage::Vertex) || m_Shaders->has_failed(path, ShaderStage::Pixel))
			{
				m_Failed = true;
			}
		}
	}

	bool PickPass::create_target(u32 width, u32 height)
	{
		nvrhi::TextureDesc id;
		id.width = width;
		id.height = height;
		id.format = nvrhi::Format::R32_UINT;
		id.isRenderTarget = true;
		id.initialState = nvrhi::ResourceStates::RenderTarget;
		id.keepInitialState = true;
		id.debugName = "Pick IDs";
		m_IdTarget = m_Device->createTexture(id);

		nvrhi::TextureDesc depth;
		depth.width = width;
		depth.height = height;
		depth.format = nvrhi::Format::D32;
		depth.isRenderTarget = true;
		depth.isShaderResource = false; // only tested against: D3D11 cannot make a sampled view of a plain D32 texture
		depth.initialState = nvrhi::ResourceStates::DepthWrite;
		depth.keepInitialState = true;
		depth.useClearValue = true;
		depth.clearValue = nvrhi::Color(1.0f, 0.0f, 0.0f, 0.0f);
		depth.debugName = "Pick depth";
		m_IdDepth = m_Device->createTexture(depth);

		nvrhi::TextureDesc staging;
		staging.width = 1;
		staging.height = 1;
		staging.format = nvrhi::Format::R32_UINT;
		staging.debugName = "Pick readback";
		m_Staging = m_Device->createStagingTexture(staging, nvrhi::CpuAccessMode::Read);

		if (m_IdTarget == nullptr || m_IdDepth == nullptr || m_Staging == nullptr)
		{
			release_target();
			return false;
		}

		nvrhi::FramebufferDesc framebuffer;
		framebuffer.addColorAttachment(m_IdTarget);
		framebuffer.setDepthAttachment(m_IdDepth);
		m_IdFramebuffer = m_Device->createFramebuffer(framebuffer);
		if (m_IdFramebuffer == nullptr)
		{
			release_target();
			return false;
		}
		return true;
	}

	void PickPass::release_target()
	{
		m_IdFramebuffer = nullptr;
		m_IdTarget = nullptr;
		m_IdDepth = nullptr;
		m_Staging = nullptr;
		m_Recorded = false;
	}

	bool PickPass::build_pipelines(const SceneRenderData& data, nvrhi::IFramebuffer* framebuffer)
	{
		nvrhi::IShader* vertex = m_Shaders->get(engine_assets::PICK_SHADER, ShaderStage::Vertex);
		nvrhi::IShader* pixel = m_Shaders->get(engine_assets::PICK_SHADER, ShaderStage::Pixel);
		m_InputLayout = scene_pass::make_mesh_input_layout(m_Device, vertex);
		const std::vector<nvrhi::IBindingLayout*> layouts = { data.frame_layout() };

		for (int doubleSided = 0; doubleSided < 2; doubleSided++)
		{
			scene_pass::PipelineOptions options;
			options.DoubleSided = doubleSided != 0;
			options.DepthTest = true;
			options.DepthWrite = true;
			m_InstancePipelines[doubleSided] = scene_pass::make_pipeline(m_Device, m_InputLayout, vertex, pixel, layouts, framebuffer, options);
			if (m_InstancePipelines[doubleSided] == nullptr)
			{
				return false;
			}
		}

		nvrhi::GraphicsPipelineDesc icons;
		icons.primType = nvrhi::PrimitiveType::TriangleList;
		icons.VS = m_Shaders->get(engine_assets::PICK_ICON_SHADER, ShaderStage::Vertex);
		icons.PS = m_Shaders->get(engine_assets::PICK_ICON_SHADER, ShaderStage::Pixel);
		icons.addBindingLayout(m_IconLayout);
		icons.renderState.depthStencilState.depthTestEnable = true;
		icons.renderState.depthStencilState.depthWriteEnable = true;
		icons.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::LessOrEqual;
		icons.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
		m_IconPipeline = m_Device->createGraphicsPipeline(icons, framebuffer);
		return m_IconPipeline != nullptr;
	}

	void PickPass::render(nvrhi::ICommandList* commandList, const FrameDescription& frame)
	{
		m_Recorded = false;
		if (m_Failed || frame.Pick == nullptr || !frame.Pick->Pending)
		{
			return;
		}

		const PickRequest request = *frame.Pick->Pending;
		if (frame.Scene == nullptr || !frame.Scene->active() || !frame.Scene->has_camera())
		{
			return; // answered with nothing after the frame
		}

		const SceneRenderData& data = *frame.Scene;
		ViewportTarget& viewport = *data.viewport();
		if (request.X >= viewport.width() || request.Y >= viewport.height())
		{
			return;
		}

		release_target();
		if (!create_target(viewport.width(), viewport.height()))
		{
			WI_PICK_LOG(Error, "Could not create the pick target");
			m_Failed = true;
			return;
		}
		frame.Pick->TargetAlive = true;

		const u32 shaderGeneration = m_Shaders->get_generation();
		if (shaderGeneration != m_BuiltShaderGeneration || m_InstancePipelines[0] == nullptr)
		{
			m_BuiltShaderGeneration = shaderGeneration;
			if (!build_pipelines(data, m_IdFramebuffer))
			{
				WI_PICK_LOG(Error, "Could not create the pick pipelines");
				m_Failed = true;
				return;
			}
		}

		commandList->clearTextureUInt(m_IdTarget, nvrhi::AllSubresources, 0);
		commandList->clearDepthStencilTexture(m_IdDepth, nvrhi::AllSubresources, true, 1.0f, false, 0);

		const nvrhi::Viewport area(static_cast<float>(viewport.width()), static_cast<float>(viewport.height()));
		for (const std::vector<DrawItem>* items : { &data.opaque(), &data.masked(), &data.blended() })
		{
			scene_pass::draw_items(commandList, data, m_IdFramebuffer, area, data.frame_bindings(), *items, m_InstancePipelines[0], m_InstancePipelines[1], false);
		}

		// The icons of this frame, over the geometry (depth tested against it).
		const u32 iconCount = frame.Debug != nullptr ? static_cast<u32>(frame.Debug->PickIcons.size()) : 0;
		if (iconCount > 0)
		{
			m_IconScratch.clear();
			for (u32 i = 0; i < iconCount; i++)
			{
				const DebugPickIcon& icon = frame.Debug->PickIcons[i];
				PickIconGpu gpu;
				gpu.Position = icon.Position;
				gpu.RadiusPx = icon.RadiusPx;
				gpu.Id = ICON_ID_BIT | i;
				m_IconScratch.push_back(gpu);
			}

			bool changed = false;
			if (m_IconBuffer == nullptr || iconCount > m_IconCapacity)
			{
				m_IconCapacity = std::max(iconCount, 256u);
				nvrhi::BufferDesc buffer;
				buffer.byteSize = static_cast<u64>(m_IconCapacity) * sizeof(PickIconGpu);
				buffer.structStride = sizeof(PickIconGpu);
				buffer.initialState = nvrhi::ResourceStates::ShaderResource;
				buffer.keepInitialState = true;
				buffer.debugName = "Pick icons";
				m_IconBuffer = m_Device->createBuffer(buffer);
				changed = true;
			}
			if (m_IconBuffer != nullptr && (changed || m_IconSet == nullptr))
			{
				nvrhi::BindingSetDesc set;
				set.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_IconBuffer));
				set.addItem(nvrhi::BindingSetItem::PushConstants(1, sizeof(IconConstants)));
				m_IconSet = m_Device->createBindingSet(set, m_IconLayout);
			}
			if (m_IconBuffer == nullptr || m_IconSet == nullptr)
			{
				WI_PICK_LOG(Error, "Could not create the pick icon buffer");
				m_Failed = true;
				return;
			}

			commandList->writeBuffer(m_IconBuffer, m_IconScratch.data(), m_IconScratch.size() * sizeof(PickIconGpu));

			IconConstants constants;
			constants.ViewProjection = data.constants().UnjitteredViewProjection;
			constants.Viewport = glm::vec2(static_cast<float>(viewport.width()), static_cast<float>(viewport.height()));

			nvrhi::GraphicsState state;
			state.pipeline = m_IconPipeline;
			state.framebuffer = m_IdFramebuffer;
			state.viewport.addViewportAndScissorRect(area);
			state.addBindingSet(m_IconSet);
			commandList->setGraphicsState(state);
			commandList->setPushConstants(&constants, sizeof(constants));
			nvrhi::DrawArguments arguments;
			arguments.vertexCount = iconCount * 6;
			commandList->draw(arguments);
		}

		nvrhi::TextureSlice source;
		source.x = request.X;
		source.y = request.Y;
		source.width = 1;
		source.height = 1;
		source.depth = 1;
		nvrhi::TextureSlice destination;
		destination.width = 1;
		destination.height = 1;
		destination.depth = 1;
		commandList->copyTexture(m_Staging, destination, m_IdTarget, source);
		m_Recorded = true;
	}

	void PickPass::on_frame_submitted(nvrhi::IDevice* device, const FrameDescription& frame)
	{
		if (frame.Pick == nullptr || !frame.Pick->Pending)
		{
			return;
		}

		PickResult result;
		result.RequestId = frame.Pick->Pending->Id;

		if (m_Recorded && m_Staging != nullptr)
		{
			device->waitForIdle();

			nvrhi::TextureSlice slice;
			slice.width = 1;
			slice.height = 1;
			slice.depth = 1;
			size_t rowPitch = 0;
			const u32* mapped = static_cast<const u32*>(device->mapStagingTexture(m_Staging, slice, nvrhi::CpuAccessMode::Read, &rowPitch));
			u32 value = 0;
			if (mapped != nullptr)
			{
				value = mapped[0];
				device->unmapStagingTexture(m_Staging);
			}

			if ((value & ICON_ID_BIT) != 0)
			{
				const u32 index = value & ~ICON_ID_BIT;
				if (frame.Debug != nullptr && index < frame.Debug->PickIcons.size())
				{
					result.Guid = frame.Debug->PickIcons[index].Guid;
				}
			}
			else if (value != 0 && frame.Scene != nullptr && frame.Scene->scene() != nullptr)
			{
				const std::vector<RenderInstance>& instances = frame.Scene->scene()->instances();
				const size_t slot = value - 1;
				if (slot < instances.size() && instances[slot].Alive)
				{
					result.Guid = instances[slot].Guid;
				}
			}
		}

		if (frame.Pick->Channel != nullptr)
		{
			frame.Pick->Channel->publish(std::move(result));
		}
		frame.Pick->Pending.reset();
		release_target();
		frame.Pick->TargetAlive = false;
	}
}
