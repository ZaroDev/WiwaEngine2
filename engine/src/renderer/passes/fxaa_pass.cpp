#include <pch.h>
#include "fxaa_pass.h"

#include "../engine_assets.h"
#include "../scene/scene_render_data.h"

#define WI_FXAA_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	bool FxaaPass::initialize(const PassInitializationDescription& description)
	{
		m_Device = description.Device;
		m_Shaders = description.Shaders;
		if (m_Shaders == nullptr)
		{
			WI_FXAA_LOG(Error, "The FXAA pass needs the shader library");
			return false;
		}

		m_Shaders->request(engine_assets::FXAA_SHADER, ShaderStage::Vertex);
		m_Shaders->request(engine_assets::FXAA_SHADER, ShaderStage::Pixel);

		m_Sampler = m_Device->createSampler(nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));

		// Slots match engine/shaders/fxaa.hlsl: SRV 0, sampler 1, push constants 2.
		nvrhi::VulkanBindingOffsets offsets;
		offsets.shaderResource = 0;
		offsets.sampler = 0;
		offsets.constantBuffer = 0;
		offsets.unorderedAccess = 0;
		nvrhi::BindingLayoutDesc layout;
		layout.visibility = nvrhi::ShaderType::All;
		layout.bindingOffsets = offsets;
		layout.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0));
		layout.addItem(nvrhi::BindingLayoutItem::Sampler(1));
		layout.addItem(nvrhi::BindingLayoutItem::PushConstants(2, sizeof(PushConstants)));
		m_BindingLayout = m_Device->createBindingLayout(layout);
		return m_Sampler && m_BindingLayout;
	}

	bool FxaaPass::is_ready() const
	{
		return m_Shaders->get(engine_assets::FXAA_SHADER, ShaderStage::Vertex) != nullptr && m_Shaders->get(engine_assets::FXAA_SHADER, ShaderStage::Pixel) != nullptr;
	}

	void FxaaPass::update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer)
	{
		if (m_Shaders->has_failed(engine_assets::FXAA_SHADER, ShaderStage::Vertex) || m_Shaders->has_failed(engine_assets::FXAA_SHADER, ShaderStage::Pixel))
		{
			m_Failed = true;
		}
	}

	void FxaaPass::render(nvrhi::ICommandList* commandList, const FrameDescription& frame)
	{
		if (frame.Scene == nullptr || !frame.Scene->active() || m_Failed || !frame.Scene->quality().Fxaa)
		{
			return;
		}

		ViewportTarget& viewport = *frame.Scene->viewport();
		const u32 shaderGeneration = m_Shaders->get_generation();
		if (shaderGeneration != m_BuiltShaderGeneration || viewport.generation() != m_BuiltViewportGeneration)
		{
			m_BuiltShaderGeneration = shaderGeneration;
			m_BuiltViewportGeneration = viewport.generation();

			nvrhi::GraphicsPipelineDesc description;
			description.primType = nvrhi::PrimitiveType::TriangleList;
			description.VS = m_Shaders->get(engine_assets::FXAA_SHADER, ShaderStage::Vertex);
			description.PS = m_Shaders->get(engine_assets::FXAA_SHADER, ShaderStage::Pixel);
			description.addBindingLayout(m_BindingLayout);
			description.renderState.depthStencilState.depthTestEnable = false;
			description.renderState.depthStencilState.depthWriteEnable = false;
			description.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
			m_Pipeline = m_Device->createGraphicsPipeline(description, viewport.ldr_framebuffer());

			nvrhi::BindingSetDesc set;
			set.addItem(nvrhi::BindingSetItem::Texture_SRV(0, viewport.pre_fxaa()));
			set.addItem(nvrhi::BindingSetItem::Sampler(1, m_Sampler));
			set.addItem(nvrhi::BindingSetItem::PushConstants(2, sizeof(PushConstants)));
			m_BindingSet = m_Device->createBindingSet(set, m_BindingLayout);

			if (m_Pipeline == nullptr || m_BindingSet == nullptr)
			{
				WI_FXAA_LOG(Error, "Could not create the FXAA pipeline");
				m_Failed = true;
				return;
			}
		}

		nvrhi::GraphicsState state;
		state.pipeline = m_Pipeline;
		state.framebuffer = viewport.ldr_framebuffer();
		state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<float>(viewport.width()), static_cast<float>(viewport.height())));
		state.addBindingSet(m_BindingSet);
		commandList->setGraphicsState(state);

		PushConstants constants;
		constants.RcpWidth = 1.0f / static_cast<float>(viewport.width());
		constants.RcpHeight = 1.0f / static_cast<float>(viewport.height());
		commandList->setPushConstants(&constants, sizeof(constants));

		nvrhi::DrawArguments arguments;
		arguments.vertexCount = 3;
		commandList->draw(arguments);
	}
}
