#include <pch.h>
#include "sky_pass.h"

#include "../engine_assets.h"
#include "../scene/scene_render_data.h"

#define WI_SKY_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	bool SkyPass::initialize(const PassInitializationDescription& description)
	{
		m_Device = description.Device;
		m_Shaders = description.Shaders;
		if (m_Shaders == nullptr)
		{
			WI_SKY_LOG(Error, "The sky pass needs the shader library");
			return false;
		}

		m_Shaders->request(engine_assets::SKY_SHADER, ShaderStage::Vertex);
		m_Shaders->request(engine_assets::SKY_SHADER, ShaderStage::Pixel);
		m_Shaders->request(engine_assets::SKY_ATMOSPHERE_SHADER, ShaderStage::Vertex);
		m_Shaders->request(engine_assets::SKY_ATMOSPHERE_SHADER, ShaderStage::Pixel);
		return true;
	}

	bool SkyPass::is_ready() const
	{
		return m_Shaders->get(engine_assets::SKY_SHADER, ShaderStage::Vertex) != nullptr && m_Shaders->get(engine_assets::SKY_SHADER, ShaderStage::Pixel) != nullptr
			&& m_Shaders->get(engine_assets::SKY_ATMOSPHERE_SHADER, ShaderStage::Vertex) != nullptr && m_Shaders->get(engine_assets::SKY_ATMOSPHERE_SHADER, ShaderStage::Pixel) != nullptr;
	}

	void SkyPass::update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer)
	{
		if (m_Shaders->has_failed(engine_assets::SKY_SHADER, ShaderStage::Vertex) || m_Shaders->has_failed(engine_assets::SKY_SHADER, ShaderStage::Pixel)
			|| m_Shaders->has_failed(engine_assets::SKY_ATMOSPHERE_SHADER, ShaderStage::Vertex) || m_Shaders->has_failed(engine_assets::SKY_ATMOSPHERE_SHADER, ShaderStage::Pixel))
		{
			m_Failed = true;
		}
	}

	void SkyPass::render(nvrhi::ICommandList* commandList, const FrameDescription& frame)
	{
		if (frame.Scene == nullptr || !frame.Scene->active() || !frame.Scene->has_camera() || m_Failed)
		{
			return;
		}

		const SceneRenderData& data = *frame.Scene;
		if (data.settings().Background != BackgroundMode::Sky || data.sky_kind() == SceneRenderData::SkyKind::None)
		{
			return;
		}

		const u32 shaderGeneration = m_Shaders->get_generation();
		if (m_Pipeline == nullptr || shaderGeneration != m_BuiltShaderGeneration || data.viewport()->generation() != m_BuiltViewportGeneration)
		{
			m_BuiltShaderGeneration = shaderGeneration;
			m_BuiltViewportGeneration = data.viewport()->generation();

			auto make = [&](const char* shader)
			{
				nvrhi::GraphicsPipelineDesc description;
				description.primType = nvrhi::PrimitiveType::TriangleList;
				description.VS = m_Shaders->get(shader, ShaderStage::Vertex);
				description.PS = m_Shaders->get(shader, ShaderStage::Pixel);
				description.addBindingLayout(data.frame_layout());
				description.renderState.depthStencilState.depthTestEnable = true;
				description.renderState.depthStencilState.depthWriteEnable = false;
				description.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::LessOrEqual;
				description.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
				return m_Device->createGraphicsPipeline(description, data.viewport()->hdr_framebuffer());
			};
			m_Pipeline = make(engine_assets::SKY_SHADER);
			m_AtmospherePipeline = make(engine_assets::SKY_ATMOSPHERE_SHADER);
			if (m_Pipeline == nullptr || m_AtmospherePipeline == nullptr)
			{
				WI_SKY_LOG(Error, "Could not create the sky pipelines");
				m_Failed = true;
				return;
			}
		}

		nvrhi::GraphicsState state;
		state.pipeline = data.sky_kind() == SceneRenderData::SkyKind::Atmosphere ? m_AtmospherePipeline : m_Pipeline;
		state.framebuffer = data.viewport()->hdr_framebuffer();
		state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<float>(data.viewport()->width()), static_cast<float>(data.viewport()->height())));
		state.addBindingSet(data.frame_bindings());
		commandList->setGraphicsState(state);

		const u32 drawConstants[3] = {}; // the frame layout has push constants (instance, material, base vertex): the sky uses none, but they must be set
		commandList->setPushConstants(drawConstants, sizeof(drawConstants));

		nvrhi::DrawArguments arguments;
		arguments.vertexCount = 3;
		commandList->draw(arguments);
	}
}
