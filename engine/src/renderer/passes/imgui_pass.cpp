#include <pch.h>
#include "imgui_pass.h"

#include "../engine_assets.h"

#define WI_IMGUI_LOG(level, ...) WI_LOG(level, "ImGui", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		// With zero offsets the Vulkan descriptor binding equals the NVRHI slot, which matches the register numbers in the
		// HLSL (DXC maps bN / tN / sN to binding N). D3D ignores this: the slot is the register number within its type.
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

	bool ImGuiRenderPass::initialize(const PassInitializationDescription& description)
	{
		m_Device = description.Device;
		m_Shaders = description.Shaders;
		if (m_Shaders == nullptr)
		{
			WI_IMGUI_LOG(Error, "The ImGui pass needs the shader library");
			return false;
		}

		m_Shaders->request(engine_assets::imgui_shader(), ShaderStage::Vertex);
		m_Shaders->request(engine_assets::imgui_shader(), ShaderStage::Pixel);

		const nvrhi::SamplerDesc samplerDescription = nvrhi::SamplerDesc()
			.setAllFilters(true)
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap);
		m_Sampler = m_Device->createSampler(samplerDescription);

		// Slots match engine/shaders/imgui.hlsl: SRV 0, sampler 1, push constants 2.
		nvrhi::BindingLayoutDesc layoutDescription;
		layoutDescription.visibility = nvrhi::ShaderType::All;
		layoutDescription.bindingOffsets = make_zero_binding_offsets();
		layoutDescription.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0));
		layoutDescription.addItem(nvrhi::BindingLayoutItem::Sampler(1));
		layoutDescription.addItem(nvrhi::BindingLayoutItem::PushConstants(2, sizeof(PushConstants)));
		m_BindingLayout = m_Device->createBindingLayout(layoutDescription);

		return m_Sampler && m_BindingLayout;
	}

	void ImGuiRenderPass::set_font_atlas(u32 width, u32 height, const u8* pixels)
	{
		nvrhi::TextureDesc description;
		description.width = width;
		description.height = height;
		description.format = nvrhi::Format::RGBA8_UNORM;
		description.initialState = nvrhi::ResourceStates::ShaderResource;
		description.keepInitialState = true;
		description.debugName = "ImGui font atlas";
		m_FontTexture = m_Device->createTexture(description);
		if (!m_FontTexture)
		{
			WI_IMGUI_LOG(Error, "Could not create the font texture");
			m_Failed = true;
			return;
		}

		nvrhi::CommandListHandle uploadList = m_Device->createCommandList();
		uploadList->open();
		uploadList->writeTexture(m_FontTexture, 0, 0, pixels, static_cast<size_t>(width) * 4);
		uploadList->close();
		m_Device->executeCommandList(uploadList);

		create_binding_set();
	}

	void ImGuiRenderPass::create_binding_set()
	{
		nvrhi::BindingSetDesc setDescription;
		setDescription.addItem(nvrhi::BindingSetItem::Texture_SRV(0, m_FontTexture));
		setDescription.addItem(nvrhi::BindingSetItem::Sampler(1, m_Sampler));
		setDescription.addItem(nvrhi::BindingSetItem::PushConstants(2, sizeof(PushConstants)));
		m_BindingSet = m_Device->createBindingSet(setDescription, m_BindingLayout);
	}

	void ImGuiRenderPass::update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer)
	{
		if (m_Failed)
		{
			return;
		}

		const AssetRef shader = engine_assets::imgui_shader();
		if (m_Shaders->has_failed(shader, ShaderStage::Vertex) || m_Shaders->has_failed(shader, ShaderStage::Pixel))
		{
			m_Failed = true;
			return;
		}

		// Built when both shaders exist, and again whenever one of them was replaced (hot reload).
		nvrhi::IShader* vertexShader = m_Shaders->get(shader, ShaderStage::Vertex);
		nvrhi::IShader* pixelShader = m_Shaders->get(shader, ShaderStage::Pixel);
		if (vertexShader == nullptr || pixelShader == nullptr || m_Shaders->get_generation() == m_BuiltGeneration)
		{
			return;
		}

		nvrhi::VertexAttributeDesc attributes[3];
		attributes[0].name = "POSITION";
		attributes[0].format = nvrhi::Format::RG32_FLOAT;
		attributes[0].offset = offsetof(ImDrawVert, pos);
		attributes[0].elementStride = sizeof(ImDrawVert);
		attributes[1].name = "TEXCOORD";
		attributes[1].format = nvrhi::Format::RG32_FLOAT;
		attributes[1].offset = offsetof(ImDrawVert, uv);
		attributes[1].elementStride = sizeof(ImDrawVert);
		attributes[2].name = "COLOR";
		attributes[2].format = nvrhi::Format::RGBA8_UNORM;
		attributes[2].offset = offsetof(ImDrawVert, col);
		attributes[2].elementStride = sizeof(ImDrawVert);
		nvrhi::InputLayoutHandle inputLayout = device->createInputLayout(attributes, 3, vertexShader);

		nvrhi::GraphicsPipelineDesc pipelineDescription;
		pipelineDescription.primType = nvrhi::PrimitiveType::TriangleList;
		pipelineDescription.inputLayout = inputLayout;
		pipelineDescription.VS = vertexShader;
		pipelineDescription.PS = pixelShader;
		pipelineDescription.addBindingLayout(m_BindingLayout);
		pipelineDescription.renderState.depthStencilState.depthTestEnable = false;
		pipelineDescription.renderState.depthStencilState.depthWriteEnable = false;
		pipelineDescription.renderState.depthStencilState.stencilEnable = false;
		pipelineDescription.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
		pipelineDescription.renderState.rasterState.scissorEnable = true;

		nvrhi::BlendState::RenderTarget& blend = pipelineDescription.renderState.blendState.targets[0];
		blend.blendEnable = true;
		blend.srcBlend = nvrhi::BlendFactor::SrcAlpha;
		blend.destBlend = nvrhi::BlendFactor::InvSrcAlpha;
		blend.blendOp = nvrhi::BlendOp::Add;
		blend.srcBlendAlpha = nvrhi::BlendFactor::InvSrcAlpha;
		blend.destBlendAlpha = nvrhi::BlendFactor::Zero;
		blend.blendOpAlpha = nvrhi::BlendOp::Add;

		nvrhi::GraphicsPipelineHandle pipeline = device->createGraphicsPipeline(pipelineDescription, backbuffer);
		if (pipeline == nullptr)
		{
			WI_IMGUI_LOG(Error, "Could not create the pipeline");
			// a reload that fails keeps the pipeline in use
			m_Failed = m_Pipeline == nullptr;
			m_BuiltGeneration = m_Shaders->get_generation();
			return;
		}

		m_InputLayout = std::move(inputLayout);
		m_Pipeline = std::move(pipeline);
		m_BuiltGeneration = m_Shaders->get_generation();
	}

	void ImGuiRenderPass::ensure_buffer_capacity(u32 vertexCount, u32 indexCount)
	{
		if (m_VertexBuffer == nullptr || vertexCount > m_VertexCapacity)
		{
			m_VertexCapacity = vertexCount + 5000; // headroom to avoid reallocating every frame
			nvrhi::BufferDesc description;
			description.byteSize = static_cast<u64>(m_VertexCapacity) * sizeof(ImDrawVert);
			description.isVertexBuffer = true;
			description.initialState = nvrhi::ResourceStates::VertexBuffer;
			description.keepInitialState = true;
			description.debugName = "ImGui vertices";
			m_VertexBuffer = m_Device->createBuffer(description);
		}

		if (m_IndexBuffer == nullptr || indexCount > m_IndexCapacity)
		{
			m_IndexCapacity = indexCount + 10000;
			nvrhi::BufferDesc description;
			description.byteSize = static_cast<u64>(m_IndexCapacity) * sizeof(ImDrawIdx);
			description.isIndexBuffer = true;
			description.initialState = nvrhi::ResourceStates::IndexBuffer;
			description.keepInitialState = true;
			description.debugName = "ImGui indices";
			m_IndexBuffer = m_Device->createBuffer(description);
		}
	}

	void ImGuiRenderPass::render(nvrhi::ICommandList* commandList, const FrameDescription& frame)
	{
		const OwnedDrawData* drawData = frame.ImGui;
		if (drawData == nullptr || !drawData->Valid || m_Pipeline == nullptr || m_BindingSet == nullptr)
		{
			return;
		}

		const float framebufferWidth = drawData->DisplaySize[0] * drawData->FramebufferScale[0];
		const float framebufferHeight = drawData->DisplaySize[1] * drawData->FramebufferScale[1];
		if (framebufferWidth <= 0.0f || framebufferHeight <= 0.0f)
		{
			return;
		}

		ensure_buffer_capacity(static_cast<u32>(drawData->Vertices.size()), static_cast<u32>(drawData->Indices.size()));
		commandList->writeBuffer(m_VertexBuffer, drawData->Vertices.data(), drawData->Vertices.size() * sizeof(ImDrawVert));
		commandList->writeBuffer(m_IndexBuffer, drawData->Indices.data(), drawData->Indices.size() * sizeof(ImDrawIdx));

		// ImGui coordinates have y pointing down; clip space has y up.
		PushConstants constants;
		constants.Scale[0] = 2.0f / drawData->DisplaySize[0];
		constants.Scale[1] = -2.0f / drawData->DisplaySize[1];
		constants.Translate[0] = -1.0f - drawData->DisplayPos[0] * constants.Scale[0];
		constants.Translate[1] = 1.0f - drawData->DisplayPos[1] * constants.Scale[1];

		nvrhi::VertexBufferBinding vertexBinding;
		vertexBinding.buffer = m_VertexBuffer;
		vertexBinding.slot = 0;
		vertexBinding.offset = 0;

		nvrhi::GraphicsState state;
		state.pipeline = m_Pipeline;
		state.framebuffer = frame.Backbuffer;
		state.addVertexBuffer(vertexBinding);
		state.indexBuffer.buffer = m_IndexBuffer;
		state.indexBuffer.format = (sizeof(ImDrawIdx) == 2) ? nvrhi::Format::R16_UINT : nvrhi::Format::R32_UINT;
		state.indexBuffer.offset = 0;
		state.addBindingSet(m_BindingSet);
		state.viewport.addViewport(nvrhi::Viewport(framebufferWidth, framebufferHeight));
		state.viewport.addScissorRect(nvrhi::Rect(0, 0, 0, 0));

		// The state only changes with the scissor rectangle: commands that share one share the state.
		bool stateSet = false;
		for (const OwnedDrawCommand& command : drawData->Commands)
		{
			const float clipMinX = std::max((command.ClipRect[0] - drawData->DisplayPos[0]) * drawData->FramebufferScale[0], 0.0f);
			const float clipMinY = std::max((command.ClipRect[1] - drawData->DisplayPos[1]) * drawData->FramebufferScale[1], 0.0f);
			const float clipMaxX = std::min((command.ClipRect[2] - drawData->DisplayPos[0]) * drawData->FramebufferScale[0], framebufferWidth);
			const float clipMaxY = std::min((command.ClipRect[3] - drawData->DisplayPos[1]) * drawData->FramebufferScale[1], framebufferHeight);
			if (clipMaxX <= clipMinX || clipMaxY <= clipMinY)
			{
				continue;
			}

			const nvrhi::Rect scissor(
				static_cast<int>(clipMinX), static_cast<int>(clipMaxX),
				static_cast<int>(clipMinY), static_cast<int>(clipMaxY));
			if (!stateSet || scissor != state.viewport.scissorRects[0])
			{
				state.viewport.scissorRects[0] = scissor;
				commandList->setGraphicsState(state);
				commandList->setPushConstants(&constants, sizeof(constants));
				stateSet = true;
			}

			nvrhi::DrawArguments arguments;
			arguments.vertexCount = command.ElementCount; // index count for indexed draws
			arguments.startIndexLocation = command.IndexOffset;
			arguments.startVertexLocation = command.VertexOffset;
			commandList->drawIndexed(arguments);
		}
	}
}
