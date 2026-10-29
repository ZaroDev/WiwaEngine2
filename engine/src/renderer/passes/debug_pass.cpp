#include <pch.h>
#include "debug_pass.h"

#include "../engine_assets.h"
#include "../scene/scene_render_data.h"

#define WI_DEBUG_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		constexpr u32 MIN_CAPACITY = 256;

		u32 grown_capacity(u32 needed)
		{
			u32 capacity = MIN_CAPACITY;
			while (capacity < needed)
			{
				capacity *= 2;
			}
			return capacity;
		}
	}

	bool DebugPass::initialize(const PassInitializationDescription& description)
	{
		m_Device = description.Device;
		m_Shaders = description.Shaders;
		if (m_Shaders == nullptr)
		{
			WI_DEBUG_LOG(Error, "The debug draw pass needs the shader library");
			return false;
		}

		for (const char* path : { engine_assets::DEBUG_LINE_SHADER, engine_assets::DEBUG_TRIANGLE_SHADER })
		{
			m_Shaders->request(path, ShaderStage::Vertex);
			m_Shaders->request(path, ShaderStage::Pixel);
		}

		// Slots match engine/shaders/debug_common.hlsli: SRV 0 lines, SRV 1 triangles, push constants 2.
		nvrhi::VulkanBindingOffsets offsets;
		offsets.shaderResource = 0;
		offsets.sampler = 0;
		offsets.constantBuffer = 0;
		offsets.unorderedAccess = 0;
		nvrhi::BindingLayoutDesc layout;
		layout.visibility = nvrhi::ShaderType::All;
		layout.bindingOffsets = offsets;
		layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(0));
		layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1));
		layout.addItem(nvrhi::BindingLayoutItem::PushConstants(2, sizeof(PushConstants)));
		m_BindingLayout = m_Device->createBindingLayout(layout);
		return m_BindingLayout != nullptr;
	}

	bool DebugPass::is_ready() const
	{
		for (const char* path : { engine_assets::DEBUG_LINE_SHADER, engine_assets::DEBUG_TRIANGLE_SHADER })
		{
			if (m_Shaders->get(path, ShaderStage::Vertex) == nullptr || m_Shaders->get(path, ShaderStage::Pixel) == nullptr)
			{
				return false;
			}
		}
		return true;
	}

	void DebugPass::update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer)
	{
		for (const char* path : { engine_assets::DEBUG_LINE_SHADER, engine_assets::DEBUG_TRIANGLE_SHADER })
		{
			if (m_Shaders->has_failed(path, ShaderStage::Vertex) || m_Shaders->has_failed(path, ShaderStage::Pixel))
			{
				m_Failed = true;
			}
		}
	}

	bool DebugPass::build_pipelines(nvrhi::IFramebuffer* framebuffer)
	{
		auto make = [&](const char* path, u32 layer)
		{
			nvrhi::GraphicsPipelineDesc description;
			description.primType = nvrhi::PrimitiveType::TriangleList;
			description.VS = m_Shaders->get(path, ShaderStage::Vertex);
			description.PS = m_Shaders->get(path, ShaderStage::Pixel);
			description.addBindingLayout(m_BindingLayout);
			description.renderState.depthStencilState.depthTestEnable = layer == 0;
			description.renderState.depthStencilState.depthWriteEnable = false;
			description.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::LessOrEqual;
			description.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;

			nvrhi::BlendState::RenderTarget& blend = description.renderState.blendState.targets[0];
			blend.blendEnable = true;
			blend.srcBlend = nvrhi::BlendFactor::SrcAlpha;
			blend.destBlend = nvrhi::BlendFactor::InvSrcAlpha;
			blend.blendOp = nvrhi::BlendOp::Add;
			blend.srcBlendAlpha = nvrhi::BlendFactor::One;
			blend.destBlendAlpha = nvrhi::BlendFactor::InvSrcAlpha;
			blend.blendOpAlpha = nvrhi::BlendOp::Add;
			return m_Device->createGraphicsPipeline(description, framebuffer);
		};

		for (u32 layer = 0; layer < 2; layer++)
		{
			m_LinePipelines[layer] = make(engine_assets::DEBUG_LINE_SHADER, layer);
			m_TrianglePipelines[layer] = make(engine_assets::DEBUG_TRIANGLE_SHADER, layer);
			if (m_LinePipelines[layer] == nullptr || m_TrianglePipelines[layer] == nullptr)
			{
				return false;
			}
		}
		return true;
	}

	bool DebugPass::ensure_buffers(u32 lineCount, u32 triangleCount)
	{
		bool changed = m_BindingSet == nullptr;
		auto ensure = [&](nvrhi::BufferHandle& buffer, u32& capacity, u32 needed, u32 stride, const char* name)
		{
			if (buffer != nullptr && needed <= capacity)
			{
				return;
			}
			capacity = grown_capacity(std::max(needed, capacity));
			nvrhi::BufferDesc description;
			description.byteSize = static_cast<u64>(capacity) * stride;
			description.structStride = stride;
			description.initialState = nvrhi::ResourceStates::ShaderResource;
			description.keepInitialState = true;
			description.debugName = name;
			buffer = m_Device->createBuffer(description);
			changed = true;
		};
		ensure(m_LineBuffer, m_LineCapacity, lineCount, sizeof(DebugLine), "Debug lines");
		ensure(m_TriangleBuffer, m_TriangleCapacity, triangleCount, sizeof(DebugTriangle), "Debug triangles");
		if (m_LineBuffer == nullptr || m_TriangleBuffer == nullptr)
		{
			return false;
		}

		if (changed)
		{
			nvrhi::BindingSetDesc set;
			set.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(0, m_LineBuffer));
			set.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(1, m_TriangleBuffer));
			set.addItem(nvrhi::BindingSetItem::PushConstants(2, sizeof(PushConstants)));
			m_BindingSet = m_Device->createBindingSet(set, m_BindingLayout);
		}
		return m_BindingSet != nullptr;
	}

	void DebugPass::render(nvrhi::ICommandList* commandList, const FrameDescription& frame)
	{
		if (m_Failed || frame.Debug == nullptr || frame.Scene == nullptr || !frame.Scene->active() || !frame.Scene->has_camera())
		{
			return;
		}

		const DebugDrawData& data = *frame.Debug;
		const u32 lineCounts[2] = { static_cast<u32>(data.Lines[0].size()), static_cast<u32>(data.Lines[1].size()) };
		const u32 triangleCounts[2] = { static_cast<u32>(data.Triangles[0].size()), static_cast<u32>(data.Triangles[1].size()) };
		if (lineCounts[0] + lineCounts[1] + triangleCounts[0] + triangleCounts[1] == 0)
		{
			return;
		}

		ViewportTarget& viewport = *frame.Scene->viewport();
		const u32 shaderGeneration = m_Shaders->get_generation();
		if (shaderGeneration != m_BuiltShaderGeneration || viewport.generation() != m_BuiltViewportGeneration)
		{
			m_BuiltShaderGeneration = shaderGeneration;
			m_BuiltViewportGeneration = viewport.generation();
			if (!build_pipelines(viewport.ldr_depth_framebuffer()))
			{
				WI_DEBUG_LOG(Error, "Could not create the debug draw pipelines");
				m_Failed = true;
				return;
			}
		}

		if (!ensure_buffers(lineCounts[0] + lineCounts[1], triangleCounts[0] + triangleCounts[1]))
		{
			WI_DEBUG_LOG(Error, "Could not create the debug draw buffers");
			m_Failed = true;
			return;
		}

		m_LineScratch.clear();
		m_LineScratch.insert(m_LineScratch.end(), data.Lines[0].begin(), data.Lines[0].end());
		m_LineScratch.insert(m_LineScratch.end(), data.Lines[1].begin(), data.Lines[1].end());
		m_TriangleScratch.clear();
		m_TriangleScratch.insert(m_TriangleScratch.end(), data.Triangles[0].begin(), data.Triangles[0].end());
		m_TriangleScratch.insert(m_TriangleScratch.end(), data.Triangles[1].begin(), data.Triangles[1].end());
		if (!m_LineScratch.empty())
		{
			commandList->writeBuffer(m_LineBuffer, m_LineScratch.data(), m_LineScratch.size() * sizeof(DebugLine));
		}
		if (!m_TriangleScratch.empty())
		{
			commandList->writeBuffer(m_TriangleBuffer, m_TriangleScratch.data(), m_TriangleScratch.size() * sizeof(DebugTriangle));
		}

		PushConstants constants;
		constants.ViewProjection = frame.Scene->constants().ViewProjection;
		constants.Viewport = glm::vec2(static_cast<float>(viewport.width()), static_cast<float>(viewport.height()));

		nvrhi::GraphicsState state;
		state.framebuffer = viewport.ldr_depth_framebuffer();
		state.viewport.addViewportAndScissorRect(nvrhi::Viewport(constants.Viewport.x, constants.Viewport.y));
		state.addBindingSet(m_BindingSet);

		auto draw = [&](nvrhi::IGraphicsPipeline* pipeline, u32 first, u32 count, u32 verticesPerShape)
		{
			if (count == 0)
			{
				return;
			}
			state.pipeline = pipeline;
			commandList->setGraphicsState(state);
			constants.FirstElement = first;
			commandList->setPushConstants(&constants, sizeof(constants));
			nvrhi::DrawArguments arguments;
			arguments.vertexCount = count * verticesPerShape;
			commandList->draw(arguments);
		};

		// Fills first, then lines; the depth-tested layer before the always-on-top one, so the dim copy of a faded shape goes over its full one.
		draw(m_TrianglePipelines[0], 0, triangleCounts[0], 3);
		draw(m_TrianglePipelines[1], triangleCounts[0], triangleCounts[1], 3);
		draw(m_LinePipelines[0], 0, lineCounts[0], 6);
		draw(m_LinePipelines[1], lineCounts[0], lineCounts[1], 6);
	}
}
