#include <pch.h>
#include "ui_pass.h"

#include "../engine_assets.h"
#include "../resources/gpu_resource_cache.h"
#include "../scene/scene_render_data.h"

#define WI_UI_PASS_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		constexpr size_t MAX_CACHED_BINDING_SETS = 64;

		// With zero offsets the Vulkan descriptor binding equals the NVRHI slot, which matches the register numbers in the HLSL.
		nvrhi::VulkanBindingOffsets make_zero_binding_offsets()
		{
			nvrhi::VulkanBindingOffsets offsets;
			offsets.shaderResource = 0;
			offsets.sampler = 0;
			offsets.constantBuffer = 0;
			offsets.unorderedAccess = 0;
			return offsets;
		}
	}

	bool UiRenderPass::initialize(const PassInitializationDescription& description)
	{
		m_Device = description.Device;
		m_Shaders = description.Shaders;
		if (m_Shaders == nullptr)
		{
			WI_UI_PASS_LOG(Error, "The UI pass needs the shader library");
			return false;
		}

		m_Shaders->request(engine_assets::UI_SHADER, ShaderStage::Vertex);
		m_Shaders->request(engine_assets::UI_SHADER, ShaderStage::Pixel);

		m_Sampler = m_Device->createSampler(nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));

		// Slots match engine/shaders/ui.hlsl: SRV 0, sampler 1, push constants 2.
		nvrhi::BindingLayoutDesc layoutDescription;
		layoutDescription.visibility = nvrhi::ShaderType::All;
		layoutDescription.bindingOffsets = make_zero_binding_offsets();
		layoutDescription.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0));
		layoutDescription.addItem(nvrhi::BindingLayoutItem::Sampler(1));
		layoutDescription.addItem(nvrhi::BindingLayoutItem::PushConstants(2, sizeof(PushConstants)));
		m_BindingLayout = m_Device->createBindingLayout(layoutDescription);

		// what plain colours are drawn with
		nvrhi::TextureDesc whiteDescription;
		whiteDescription.width = 1;
		whiteDescription.height = 1;
		whiteDescription.format = nvrhi::Format::RGBA8_UNORM;
		whiteDescription.initialState = nvrhi::ResourceStates::ShaderResource;
		whiteDescription.keepInitialState = true;
		whiteDescription.debugName = "UI white";
		m_White = m_Device->createTexture(whiteDescription);
		if (m_White)
		{
			const u8 white[4] = { 255, 255, 255, 255 };
			nvrhi::CommandListHandle uploadList = m_Device->createCommandList();
			uploadList->open();
			uploadList->writeTexture(m_White, 0, 0, white, 4);
			uploadList->close();
			m_Device->executeCommandList(uploadList);
		}

		return m_Sampler && m_BindingLayout && m_White;
	}

	bool UiRenderPass::is_ready() const
	{
		return m_Shaders->get(engine_assets::UI_SHADER, ShaderStage::Vertex) != nullptr && m_Shaders->get(engine_assets::UI_SHADER, ShaderStage::Pixel) != nullptr;
	}

	void UiRenderPass::update(nvrhi::IDevice*, nvrhi::IFramebuffer*)
	{
		if (m_Shaders->has_failed(engine_assets::UI_SHADER, ShaderStage::Vertex) || m_Shaders->has_failed(engine_assets::UI_SHADER, ShaderStage::Pixel))
		{
			m_Failed = true;
		}
	}

	bool UiRenderPass::build_pipeline(nvrhi::IFramebuffer* framebuffer)
	{
		nvrhi::IShader* vertexShader = m_Shaders->get(engine_assets::UI_SHADER, ShaderStage::Vertex);
		nvrhi::IShader* pixelShader = m_Shaders->get(engine_assets::UI_SHADER, ShaderStage::Pixel);
		if (vertexShader == nullptr || pixelShader == nullptr)
		{
			return false;
		}

		nvrhi::VertexAttributeDesc attributes[3];
		attributes[0].name = "POSITION";
		attributes[0].format = nvrhi::Format::RG32_FLOAT;
		attributes[0].offset = offsetof(UiVertex, Pos);
		attributes[0].elementStride = sizeof(UiVertex);
		attributes[1].name = "TEXCOORD";
		attributes[1].format = nvrhi::Format::RG32_FLOAT;
		attributes[1].offset = offsetof(UiVertex, Uv);
		attributes[1].elementStride = sizeof(UiVertex);
		attributes[2].name = "COLOR";
		attributes[2].format = nvrhi::Format::RGBA8_UNORM;
		attributes[2].offset = offsetof(UiVertex, Color);
		attributes[2].elementStride = sizeof(UiVertex);
		nvrhi::InputLayoutHandle inputLayout = m_Device->createInputLayout(attributes, 3, vertexShader);

		nvrhi::GraphicsPipelineDesc description;
		description.primType = nvrhi::PrimitiveType::TriangleList;
		description.inputLayout = inputLayout;
		description.VS = vertexShader;
		description.PS = pixelShader;
		description.addBindingLayout(m_BindingLayout);
		description.renderState.depthStencilState.depthTestEnable = false;
		description.renderState.depthStencilState.depthWriteEnable = false;
		description.renderState.depthStencilState.stencilEnable = false;
		description.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
		description.renderState.rasterState.scissorEnable = true;

		nvrhi::BlendState::RenderTarget& blend = description.renderState.blendState.targets[0];
		blend.blendEnable = true;
		blend.srcBlend = nvrhi::BlendFactor::SrcAlpha;
		blend.destBlend = nvrhi::BlendFactor::InvSrcAlpha;
		blend.blendOp = nvrhi::BlendOp::Add;
		blend.srcBlendAlpha = nvrhi::BlendFactor::One;
		blend.destBlendAlpha = nvrhi::BlendFactor::InvSrcAlpha;
		blend.blendOpAlpha = nvrhi::BlendOp::Add;

		nvrhi::GraphicsPipelineHandle pipeline = m_Device->createGraphicsPipeline(description, framebuffer);
		if (pipeline == nullptr)
		{
			return false;
		}
		m_InputLayout = std::move(inputLayout);
		m_Pipeline = std::move(pipeline);
		return true;
	}

	bool UiRenderPass::ensure_buffers(u32 vertexCount, u32 indexCount)
	{
		if (m_VertexBuffer == nullptr || vertexCount > m_VertexCapacity)
		{
			m_VertexCapacity = std::max(vertexCount * 2, 1024u);
			nvrhi::BufferDesc description;
			description.byteSize = static_cast<u64>(m_VertexCapacity) * sizeof(UiVertex);
			description.isVertexBuffer = true;
			description.initialState = nvrhi::ResourceStates::VertexBuffer;
			description.keepInitialState = true;
			description.debugName = "UI vertices";
			m_VertexBuffer = m_Device->createBuffer(description);
		}
		if (m_IndexBuffer == nullptr || indexCount > m_IndexCapacity)
		{
			m_IndexCapacity = std::max(indexCount * 2, 2048u);
			nvrhi::BufferDesc description;
			description.byteSize = static_cast<u64>(m_IndexCapacity) * sizeof(u32);
			description.isIndexBuffer = true;
			description.initialState = nvrhi::ResourceStates::IndexBuffer;
			description.keepInitialState = true;
			description.debugName = "UI indices";
			m_IndexBuffer = m_Device->createBuffer(description);
		}
		return m_VertexBuffer != nullptr && m_IndexBuffer != nullptr;
	}

	nvrhi::IBindingSet* UiRenderPass::binding_set_for(nvrhi::ITexture* texture)
	{
		const auto found = m_BindingSets.find(texture);
		if (found != m_BindingSets.end())
		{
			return found->second.Set;
		}

		if (m_BindingSets.size() >= MAX_CACHED_BINDING_SETS)
		{
			m_BindingSets.clear(); // textures that were replaced (a font cooked again) are let go with their sets
		}

		nvrhi::BindingSetDesc setDescription;
		setDescription.addItem(nvrhi::BindingSetItem::Texture_SRV(0, texture));
		setDescription.addItem(nvrhi::BindingSetItem::Sampler(1, m_Sampler));
		setDescription.addItem(nvrhi::BindingSetItem::PushConstants(2, sizeof(PushConstants)));
		BoundTexture bound;
		bound.Texture = texture;
		bound.Set = m_Device->createBindingSet(setDescription, m_BindingLayout);
		nvrhi::IBindingSet* set = bound.Set;
		m_BindingSets[texture] = std::move(bound);
		return set;
	}

	void UiRenderPass::render(nvrhi::ICommandList* commandList, const FrameDescription& frame)
	{
		if (m_Failed || frame.Ui == nullptr || frame.Ui->empty() || frame.Scene == nullptr || !frame.Scene->active())
		{
			return;
		}

		const UiDrawList& list = *frame.Ui;
		ViewportTarget* viewport = frame.Scene->viewport();
		GpuResourceCache* cache = frame.Scene->cache();
		if (viewport == nullptr || !viewport->valid() || cache == nullptr || !(list.CanvasSize.x > 0.0f) || !(list.CanvasSize.y > 0.0f))
		{
			return;
		}

		const u32 shaderGeneration = m_Shaders->get_generation();
		if (m_Pipeline == nullptr || shaderGeneration != m_BuiltShaderGeneration || viewport->generation() != m_BuiltViewportGeneration)
		{
			m_BuiltShaderGeneration = shaderGeneration;
			m_BuiltViewportGeneration = viewport->generation();
			if (!build_pipeline(viewport->ldr_framebuffer()))
			{
				WI_UI_PASS_LOG(Error, "Could not create the UI pipeline");
				m_Failed = true;
				return;
			}
		}

		if (!ensure_buffers(static_cast<u32>(list.Vertices.size()), static_cast<u32>(list.Indices.size())))
		{
			WI_UI_PASS_LOG(Error, "Could not create the UI buffers");
			m_Failed = true;
			return;
		}
		commandList->writeBuffer(m_VertexBuffer, list.Vertices.data(), list.Vertices.size() * sizeof(UiVertex));
		commandList->writeBuffer(m_IndexBuffer, list.Indices.data(), list.Indices.size() * sizeof(u32));

		const float targetWidth = static_cast<float>(viewport->width());
		const float targetHeight = static_cast<float>(viewport->height());
		const glm::vec2 toTarget(targetWidth / list.CanvasSize.x, targetHeight / list.CanvasSize.y);

		// canvas pixels, y pointing down, to clip space
		PushConstants constants;
		constants.Scale[0] = 2.0f / list.CanvasSize.x;
		constants.Scale[1] = -2.0f / list.CanvasSize.y;
		constants.Translate[0] = -1.0f;
		constants.Translate[1] = 1.0f;

		nvrhi::VertexBufferBinding vertexBinding;
		vertexBinding.buffer = m_VertexBuffer;
		vertexBinding.slot = 0;
		vertexBinding.offset = 0;

		nvrhi::GraphicsState state;
		state.pipeline = m_Pipeline;
		state.framebuffer = viewport->ldr_framebuffer();
		state.addVertexBuffer(vertexBinding);
		state.indexBuffer.buffer = m_IndexBuffer;
		state.indexBuffer.format = nvrhi::Format::R32_UINT;
		state.indexBuffer.offset = 0;
		state.viewport.addViewport(nvrhi::Viewport(targetWidth, targetHeight));
		state.viewport.addScissorRect(nvrhi::Rect(0, 0, 0, 0));
		state.addBindingSet(binding_set_for(m_White));

		for (const UiBatch& batch : list.Batches)
		{
			if (batch.IndexCount == 0 || static_cast<size_t>(batch.FirstIndex) + batch.IndexCount > list.Indices.size())
			{
				continue;
			}

			nvrhi::ITexture* texture = nullptr;
			switch (batch.Kind)
			{
			case UiQuadKind::Solid: texture = m_White; break;
			case UiQuadKind::Image: texture = cache->get_texture(batch.Texture); break;
			case UiQuadKind::Sdf: texture = cache->get_font_atlas(batch.Texture); break;
			}
			if (texture == nullptr)
			{
				continue; // not on the GPU (yet): nothing to draw it with
			}

			const float minX = std::clamp(batch.ClipRect.x * toTarget.x, 0.0f, targetWidth);
			const float minY = std::clamp(batch.ClipRect.y * toTarget.y, 0.0f, targetHeight);
			const float maxX = std::clamp(batch.ClipRect.z * toTarget.x, 0.0f, targetWidth);
			const float maxY = std::clamp(batch.ClipRect.w * toTarget.y, 0.0f, targetHeight);
			const int scissorMinX = static_cast<int>(std::floor(minX));
			const int scissorMinY = static_cast<int>(std::floor(minY));
			const int scissorMaxX = static_cast<int>(std::ceil(maxX));
			const int scissorMaxY = static_cast<int>(std::ceil(maxY));
			if (scissorMaxX <= scissorMinX || scissorMaxY <= scissorMinY)
			{
				continue;
			}

			state.viewport.scissorRects[0] = nvrhi::Rect(scissorMinX, scissorMaxX, scissorMinY, scissorMaxY);
			state.bindings[0] = binding_set_for(texture);

			const bool text = batch.Kind == UiQuadKind::Sdf;
			constants.Params[0] = batch.SdfRange * batch.PixelScale * (toTarget.x + toTarget.y) * 0.5f;
			constants.Params[1] = batch.Outline;
			constants.Params[2] = text ? 1.0f : 0.0f;
			constants.Params[3] = batch.PixelScale * (toTarget.x + toTarget.y) * 0.5f;
			const glm::vec4 outline(static_cast<float>(batch.OutlineColor & 0xFF) / 255.0f, static_cast<float>((batch.OutlineColor >> 8) & 0xFF) / 255.0f,
				static_cast<float>((batch.OutlineColor >> 16) & 0xFF) / 255.0f, static_cast<float>((batch.OutlineColor >> 24) & 0xFF) / 255.0f);
			constants.OutlineColor[0] = outline.r;
			constants.OutlineColor[1] = outline.g;
			constants.OutlineColor[2] = outline.b;
			constants.OutlineColor[3] = outline.a;
			constants.Extra[0] = batch.Shadow ? 1.0f : 0.0f;

			commandList->setGraphicsState(state);
			commandList->setPushConstants(&constants, sizeof(constants));

			nvrhi::DrawArguments arguments;
			arguments.vertexCount = batch.IndexCount; // index count for indexed draws
			arguments.startIndexLocation = batch.FirstIndex;
			arguments.startVertexLocation = 0;
			commandList->drawIndexed(arguments);
		}
	}
}
