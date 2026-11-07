#include <pch.h>
#include "ui_pass.h"

#include "../engine_assets.h"
#include "../resources/gpu_resource_cache.h"
#include "../scene/scene_render_data.h"
#include "../texture_registry.h"

#define WI_UI_PASS_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		constexpr size_t MAX_CACHED_BINDING_SETS = 64;
		constexpr u32 MAX_PREVIEW_SIZE = 8192;

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
		m_Textures = description.Textures;
		m_Cache = description.Cache;
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

	void UiRenderPass::clear_preview()
	{
		m_PreviewList = UiDrawList{};
		m_PreviewWidth = 0;
		m_PreviewHeight = 0;
		m_PreviewFramebuffer = nullptr;
		m_PreviewTexture = nullptr;
		m_PreviewSlot = PipelineSlot{};
		if (m_Textures != nullptr)
		{
			m_Textures->set(UI_PREVIEW_TEXTURE_ID, nullptr);
		}
	}

	void UiRenderPass::set_preview(UiDrawList list, u32 width, u32 height, const glm::vec4& background)
	{
		if (width == 0 || height == 0 || m_Device == nullptr)
		{
			clear_preview();
			return;
		}

		width = std::min(width, MAX_PREVIEW_SIZE);
		height = std::min(height, MAX_PREVIEW_SIZE);
		m_PreviewList = std::move(list);
		m_PreviewBackground = background;

		if (m_PreviewTexture == nullptr || width != m_PreviewWidth || height != m_PreviewHeight)
		{
			nvrhi::TextureDesc description;
			description.width = width;
			description.height = height;
			description.format = nvrhi::Format::RGBA8_UNORM;
			description.isRenderTarget = true;
			description.isShaderResource = true;
			description.initialState = nvrhi::ResourceStates::ShaderResource;
			description.keepInitialState = true;
			description.useClearValue = true;
			description.clearValue = nvrhi::Color(0.0f, 0.0f, 0.0f, 1.0f);
			description.debugName = "UI preview";
			nvrhi::TextureHandle texture = m_Device->createTexture(description);

			nvrhi::FramebufferDesc framebufferDescription;
			framebufferDescription.addColorAttachment(texture);
			nvrhi::FramebufferHandle framebuffer = texture ? m_Device->createFramebuffer(framebufferDescription) : nvrhi::FramebufferHandle{};
			if (texture == nullptr || framebuffer == nullptr)
			{
				WI_UI_PASS_LOG(Error, "Could not create the UI preview texture");
				clear_preview();
				return;
			}

			m_PreviewTexture = std::move(texture);
			m_PreviewFramebuffer = std::move(framebuffer);
			m_PreviewWidth = width;
			m_PreviewHeight = height;
			m_PreviewGeneration++;
			if (m_Textures != nullptr)
			{
				m_Textures->set(UI_PREVIEW_TEXTURE_ID, m_PreviewTexture);
			}
		}
	}

	bool UiRenderPass::build_pipeline(nvrhi::IFramebuffer* framebuffer, PipelineSlot& slot)
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
		slot.Pipeline = std::move(pipeline);
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

	bool UiRenderPass::draw_list(nvrhi::ICommandList* commandList, const UiDrawList& list, nvrhi::IFramebuffer* framebuffer, u32 width, u32 height,
		u32 targetGeneration, GpuResourceCache& cache, PipelineSlot& slot)
	{
		if (list.empty() || !(list.CanvasSize.x > 0.0f) || !(list.CanvasSize.y > 0.0f) || width == 0 || height == 0)
		{
			return true; // nothing to draw is not a failure
		}

		const u32 shaderGeneration = m_Shaders->get_generation();
		if (slot.Pipeline == nullptr || shaderGeneration != slot.ShaderGeneration || targetGeneration != slot.TargetGeneration)
		{
			slot.ShaderGeneration = shaderGeneration;
			slot.TargetGeneration = targetGeneration;
			if (!build_pipeline(framebuffer, slot))
			{
				WI_UI_PASS_LOG(Error, "Could not create the UI pipeline");
				m_Failed = true;
				return false;
			}
		}

		if (!ensure_buffers(static_cast<u32>(list.Vertices.size()), static_cast<u32>(list.Indices.size())))
		{
			WI_UI_PASS_LOG(Error, "Could not create the UI buffers");
			m_Failed = true;
			return false;
		}
		commandList->writeBuffer(m_VertexBuffer, list.Vertices.data(), list.Vertices.size() * sizeof(UiVertex));
		commandList->writeBuffer(m_IndexBuffer, list.Indices.data(), list.Indices.size() * sizeof(u32));

		const float targetWidth = static_cast<float>(width);
		const float targetHeight = static_cast<float>(height);
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
		state.pipeline = slot.Pipeline;
		state.framebuffer = framebuffer;
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
			case UiQuadKind::Image: texture = batch.ViewTexture != 0 ? (m_Textures != nullptr ? m_Textures->get(batch.ViewTexture) : nullptr) : cache.get_texture(batch.Texture); break;
			case UiQuadKind::Sdf: texture = cache.get_font_atlas(batch.Texture); break;
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
			const float averageScale = (toTarget.x + toTarget.y) * 0.5f;
			constants.Params[0] = batch.SdfRange * batch.PixelScale * averageScale;
			constants.Params[1] = batch.Outline;
			constants.Params[2] = text ? 1.0f : 0.0f;
			constants.Params[3] = batch.PixelScale * averageScale;
			constants.OutlineColor[0] = static_cast<float>(batch.OutlineColor & 0xFF) / 255.0f;
			constants.OutlineColor[1] = static_cast<float>((batch.OutlineColor >> 8) & 0xFF) / 255.0f;
			constants.OutlineColor[2] = static_cast<float>((batch.OutlineColor >> 16) & 0xFF) / 255.0f;
			constants.OutlineColor[3] = static_cast<float>((batch.OutlineColor >> 24) & 0xFF) / 255.0f;
			constants.Extra[0] = batch.Shadow ? 1.0f : 0.0f;

			commandList->setGraphicsState(state);
			commandList->setPushConstants(&constants, sizeof(constants));

			nvrhi::DrawArguments arguments;
			arguments.vertexCount = batch.IndexCount; // index count for indexed draws
			arguments.startIndexLocation = batch.FirstIndex;
			arguments.startVertexLocation = 0;
			commandList->drawIndexed(arguments);
		}
		return true;
	}

	void UiRenderPass::render(nvrhi::ICommandList* commandList, const FrameDescription& frame)
	{
		if (m_Failed)
		{
			return;
		}

		GpuResourceCache* cache = m_Cache != nullptr ? m_Cache : (frame.Scene != nullptr ? frame.Scene->cache() : nullptr);
		if (cache == nullptr)
		{
			return;
		}

		// the editor's canvas: it needs no scene
		if (m_PreviewTexture != nullptr && m_PreviewFramebuffer != nullptr)
		{
			commandList->clearTextureFloat(m_PreviewTexture, nvrhi::AllSubresources,
				nvrhi::Color(m_PreviewBackground.r, m_PreviewBackground.g, m_PreviewBackground.b, m_PreviewBackground.a));
			if (!draw_list(commandList, m_PreviewList, m_PreviewFramebuffer, m_PreviewWidth, m_PreviewHeight, m_PreviewGeneration, *cache, m_PreviewSlot))
			{
				return;
			}
		}

		// the game's UI, over the scene
		if (frame.Ui == nullptr || frame.Ui->empty() || frame.Scene == nullptr || !frame.Scene->active())
		{
			return;
		}
		ViewportTarget* viewport = frame.Scene->viewport();
		if (viewport == nullptr || !viewport->valid())
		{
			return;
		}
		draw_list(commandList, *frame.Ui, viewport->ldr_framebuffer(), viewport->width(), viewport->height(), viewport->generation(), *cache, m_ViewportSlot);
	}
}
