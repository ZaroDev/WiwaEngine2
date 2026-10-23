#include <pch.h>
#include "imgui_pass.h"

#include <renderer/shader_utils.h>


#include "backends/imgui_impl_glfw.h"

#define WI_LOG_IMGUI(level, ...) WI_LOG(level, "ImGui", __VA_ARGS__)

namespace wiwa
{
	ImGuiPass::ImGuiPass(const ImGuiPassDescription& description)
		: m_Description(description)
	{
	}
	ImGuiPass::~ImGuiPass()
	{
		if (m_ContextCreated)
		{
			ImGui_ImplGlfw_Shutdown();
			ImGui::DestroyContext();
		}
	}
	bool ImGuiPass::initialize(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer)
	{
		m_Device = device;

		if (m_Description.Window == nullptr)
		{
			WI_LOG_IMGUI(Error, "ImGuiPassDescription::Window is nullptr!");
			return false;
		}


		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		m_ContextCreated = true;

		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.BackendRendererName = "imgui_nvrhi";
		io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset; // we honor ImDrawCmd::VtxOffset
		ImGui::StyleColorsDark();

		// "Other": the window was created with GLFW_NO_API, so the backend only handles input.
		ImGui_ImplGlfw_InitForOther(m_Description.Window, true);

		m_VertexShader = load_shader(device, nvrhi::ShaderType::Vertex, "imgui");
		m_PixelShader = load_shader(device, nvrhi::ShaderType::Pixel, "imgui");
		if (!m_VertexShader || !m_PixelShader)
		{
			return false;
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
		m_InputLayout = device->createInputLayout(attributes, 3, m_VertexShader);

		nvrhi::SamplerDesc sampler_description = nvrhi::SamplerDesc()
			.setAllFilters(true)
			.setAllAddressModes(nvrhi::SamplerAddressMode::Wrap);
		m_Sampler = device->createSampler(sampler_description);

		if (!create_font_texture(device))
		{
			return false;
		}

		nvrhi::BindingLayoutDesc layout_description;
		layout_description.visibility = nvrhi::ShaderType::All;
		layout_description.bindingOffsets = make_zero_binding_offsets();
		layout_description.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0));
		layout_description.addItem(nvrhi::BindingLayoutItem::Sampler(1));
		layout_description.addItem(nvrhi::BindingLayoutItem::PushConstants(2, sizeof(PushConstants)));
		m_BindingLayout = device->createBindingLayout(layout_description);

		nvrhi::BindingSetDesc set_description;
		set_description.addItem(nvrhi::BindingSetItem::Texture_SRV(0, m_FontTexture));
		set_description.addItem(nvrhi::BindingSetItem::Sampler(1, m_Sampler));
		set_description.addItem(nvrhi::BindingSetItem::PushConstants(2, sizeof(PushConstants)));
		m_BindingSet = device->createBindingSet(set_description, m_BindingLayout);

		nvrhi::GraphicsPipelineDesc pipeline_description;
		pipeline_description.primType = nvrhi::PrimitiveType::TriangleList;
		pipeline_description.inputLayout = m_InputLayout;
		pipeline_description.VS = m_VertexShader;
		pipeline_description.PS = m_PixelShader;
		pipeline_description.addBindingLayout(m_BindingLayout);
		pipeline_description.renderState.depthStencilState.depthTestEnable = false;
		pipeline_description.renderState.depthStencilState.depthWriteEnable = false;
		pipeline_description.renderState.depthStencilState.stencilEnable = false;
		pipeline_description.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
		pipeline_description.renderState.rasterState.scissorEnable = true;

		nvrhi::BlendState::RenderTarget& blend = pipeline_description.renderState.blendState.targets[0];
		blend.blendEnable = true;
		blend.srcBlend = nvrhi::BlendFactor::SrcAlpha;
		blend.destBlend = nvrhi::BlendFactor::InvSrcAlpha;
		blend.blendOp = nvrhi::BlendOp::Add;
		blend.srcBlendAlpha = nvrhi::BlendFactor::InvSrcAlpha;
		blend.destBlendAlpha = nvrhi::BlendFactor::Zero;
		blend.blendOpAlpha = nvrhi::BlendOp::Add;

		m_Pipeline = device->createGraphicsPipeline(pipeline_description, backbuffer);

		return m_Sampler && m_BindingLayout && m_BindingSet && m_Pipeline;
	}
	void ImGuiPass::render(nvrhi::ICommandList* command_list, const FrameDescription& frame)
	{
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		if (m_Description.BuildUi)
		{
			m_Description.BuildUi();
		}

		ImGui::Render();

		ImDrawData* draw_data = ImGui::GetDrawData();
		if (draw_data == nullptr || draw_data->CmdListsCount == 0)
		{
			return;
		}

		const float framebuffer_width = draw_data->DisplaySize.x * draw_data->FramebufferScale.x;
		const float framebuffer_height = draw_data->DisplaySize.y * draw_data->FramebufferScale.y;
		if (framebuffer_width <= 0.0f || framebuffer_height <= 0.0f)
		{
			return;
		}

		// Concatenate all draw lists into one vertex and one index buffer.
		m_Vertices.clear();
		m_Indices.clear();
		for (int i = 0; i < draw_data->CmdListsCount; i++)
		{
			const ImDrawList* draw_list = draw_data->CmdLists[i];
			m_Vertices.insert(m_Vertices.end(), draw_list->VtxBuffer.Data, draw_list->VtxBuffer.Data + draw_list->VtxBuffer.Size);
			m_Indices.insert(m_Indices.end(), draw_list->IdxBuffer.Data, draw_list->IdxBuffer.Data + draw_list->IdxBuffer.Size);
		}

		ensure_buffer_capacity(static_cast<uint32_t>(m_Vertices.size()), static_cast<uint32_t>(m_Indices.size()));
		command_list->writeBuffer(m_VertexBuffer, m_Vertices.data(), m_Vertices.size() * sizeof(ImDrawVert));
		command_list->writeBuffer(m_IndexBuffer, m_Indices.data(), m_Indices.size() * sizeof(ImDrawIdx));

		PushConstants constants;
		// ImGui coordinates have y pointing down. With y-up clip space (sign +1) Y is negated;
		// with y-down clip space (sign -1) it maps directly.
		constants.Scale[0] = 2.0f / draw_data->DisplaySize.x;
		constants.Scale[1] = -2.0f / draw_data->DisplaySize.y;
		constants.Translate[0] = -1.0f - draw_data->DisplayPos.x * constants.Scale[0];
		constants.Translate[1] = 1.0f - draw_data->DisplayPos.y * constants.Scale[1];

		nvrhi::VertexBufferBinding vertex_binding;
		vertex_binding.buffer = m_VertexBuffer;
		vertex_binding.slot = 0;
		vertex_binding.offset = 0;

		nvrhi::GraphicsState state;
		state.pipeline = m_Pipeline;
		state.framebuffer = frame.Backbuffer;
		state.addVertexBuffer(vertex_binding);
		state.indexBuffer.buffer = m_IndexBuffer;
		state.indexBuffer.format = (sizeof(ImDrawIdx) == 2) ? nvrhi::Format::R16_UINT : nvrhi::Format::R32_UINT;
		state.indexBuffer.offset = 0;
		state.addBindingSet(m_BindingSet);
		state.viewport.addViewport(nvrhi::Viewport(framebuffer_width, framebuffer_height));
		state.viewport.addScissorRect(nvrhi::Rect(0, static_cast<int>(framebuffer_width), 0, static_cast<int>(framebuffer_height)));

		const ImVec2 clip_offset = draw_data->DisplayPos;
		const ImVec2 clip_scale = draw_data->FramebufferScale;

		uint32_t vertex_offset = 0;
		uint32_t index_offset = 0;
		for (int list_index = 0; list_index < draw_data->CmdListsCount; list_index++)
		{
			const ImDrawList* draw_list = draw_data->CmdLists[list_index];

			for (int command_index = 0; command_index < draw_list->CmdBuffer.Size; command_index++)
			{
				const ImDrawCmd& draw_command = draw_list->CmdBuffer[command_index];

				if (draw_command.UserCallback != nullptr)
				{
					continue; // custom callbacks are not supported
				}

				ImVec2 clip_min((draw_command.ClipRect.x - clip_offset.x) * clip_scale.x, (draw_command.ClipRect.y - clip_offset.y) * clip_scale.y);
				ImVec2 clip_max((draw_command.ClipRect.z - clip_offset.x) * clip_scale.x, (draw_command.ClipRect.w - clip_offset.y) * clip_scale.y);
				if (clip_min.x < 0.0f) { clip_min.x = 0.0f; }
				if (clip_min.y < 0.0f) { clip_min.y = 0.0f; }
				if (clip_max.x > framebuffer_width) { clip_max.x = framebuffer_width; }
				if (clip_max.y > framebuffer_height) { clip_max.y = framebuffer_height; }
				if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
				{
					continue;
				}

				state.viewport.scissorRects[0] = nvrhi::Rect(
					static_cast<int>(clip_min.x), static_cast<int>(clip_max.x),
					static_cast<int>(clip_min.y), static_cast<int>(clip_max.y));
				command_list->setGraphicsState(state);
				command_list->setPushConstants(&constants, sizeof(constants));

				nvrhi::DrawArguments arguments;
				arguments.vertexCount = draw_command.ElemCount; // index count for indexed draws
				arguments.startIndexLocation = index_offset + draw_command.IdxOffset;
				arguments.startVertexLocation = vertex_offset + draw_command.VtxOffset;
				command_list->drawIndexed(arguments);
			}

			vertex_offset += static_cast<uint32_t>(draw_list->VtxBuffer.Size);
			index_offset += static_cast<uint32_t>(draw_list->IdxBuffer.Size);
		}
	}
	void ImGuiPass::update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer)
	{
		if (m_Pipeline)
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
		m_InputLayout = device->createInputLayout(attributes, 3, m_VertexShader);

		nvrhi::GraphicsPipelineDesc pipeline_description;
		pipeline_description.primType = nvrhi::PrimitiveType::TriangleList;
		pipeline_description.inputLayout = m_InputLayout;
		pipeline_description.VS = m_VertexShader;
		pipeline_description.PS = m_PixelShader;
		pipeline_description.addBindingLayout(m_BindingLayout);
		pipeline_description.renderState.depthStencilState.depthTestEnable = false;
		pipeline_description.renderState.depthStencilState.depthWriteEnable = false;
		pipeline_description.renderState.depthStencilState.stencilEnable = false;
		pipeline_description.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
		pipeline_description.renderState.rasterState.scissorEnable = true;

		nvrhi::BlendState::RenderTarget& blend = pipeline_description.renderState.blendState.targets[0];
		blend.blendEnable = true;
		blend.srcBlend = nvrhi::BlendFactor::SrcAlpha;
		blend.destBlend = nvrhi::BlendFactor::InvSrcAlpha;
		blend.blendOp = nvrhi::BlendOp::Add;
		blend.srcBlendAlpha = nvrhi::BlendFactor::InvSrcAlpha;
		blend.destBlendAlpha = nvrhi::BlendFactor::Zero;
		blend.blendOpAlpha = nvrhi::BlendOp::Add;

		m_Pipeline = device->createGraphicsPipeline(pipeline_description, backbuffer);
	}
	bool ImGuiPass::create_font_texture(nvrhi::IDevice* device)
	{
		ImGuiIO& io = ImGui::GetIO();

		unsigned char* pixels = nullptr;
		int width = 0;
		int height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

		nvrhi::TextureDesc description;
		description.width = static_cast<uint32_t>(width);
		description.height = static_cast<uint32_t>(height);
		description.format = nvrhi::Format::RGBA8_UNORM;
		description.initialState = nvrhi::ResourceStates::ShaderResource;
		description.keepInitialState = true;
		description.debugName = "ImGui font atlas";
		m_FontTexture = device->createTexture(description);
		if (!m_FontTexture)
		{
			return false;
		}

		nvrhi::CommandListHandle upload_list = device->createCommandList();
		upload_list->open();
		upload_list->writeTexture(m_FontTexture, 0, 0, pixels, static_cast<size_t>(width) * 4);
		upload_list->close();
		device->executeCommandList(upload_list);

		// Only one texture is ever drawn, so the ID is just a non-null marker.
		io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(m_FontTexture.Get()));
		return true;
	}
	void ImGuiPass::ensure_buffer_capacity(uint32_t vertex_count, uint32_t index_count)
	{
		if (m_VertexBuffer == nullptr || vertex_count > m_VertexCapacity)
		{
			m_VertexCapacity = vertex_count + 5000; // headroom to avoid reallocating every frame

			nvrhi::BufferDesc description;
			description.byteSize = static_cast<uint64_t>(m_VertexCapacity) * sizeof(ImDrawVert);
			description.isVertexBuffer = true;
			description.initialState = nvrhi::ResourceStates::VertexBuffer;
			description.keepInitialState = true;
			description.debugName = "ImGui vertices";
			m_VertexBuffer = m_Device->createBuffer(description);
		}

		if (m_IndexBuffer == nullptr || index_count > m_IndexCapacity)
		{
			m_IndexCapacity = index_count + 10000;

			nvrhi::BufferDesc description;
			description.byteSize = static_cast<uint64_t>(m_IndexCapacity) * sizeof(ImDrawIdx);
			description.isIndexBuffer = true;
			description.initialState = nvrhi::ResourceStates::IndexBuffer;
			description.keepInitialState = true;
			description.debugName = "ImGui indices";
			m_IndexBuffer = m_Device->createBuffer(description);
		}
	}
}