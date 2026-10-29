#include <pch.h>
#include "blit_pass.h"

#include "../engine_assets.h"
#include "../scene/scene_render_data.h"

#define WI_BLIT_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	bool BlitPass::initialize(const PassInitializationDescription& description)
	{
		m_Device = description.Device;
		m_Shaders = description.Shaders;
		if (m_Shaders == nullptr)
		{
			WI_BLIT_LOG(Error, "The blit pass needs the shader library");
			return false;
		}

		m_Shaders->request(engine_assets::BLIT_SHADER, ShaderStage::Vertex);
		m_Shaders->request(engine_assets::BLIT_SHADER, ShaderStage::Pixel);

		const nvrhi::SamplerDesc samplerDescription = nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp);
		m_Sampler = m_Device->createSampler(samplerDescription);

		nvrhi::VulkanBindingOffsets offsets;
		offsets.shaderResource = 0;
		offsets.sampler = 0;
		offsets.constantBuffer = 0;
		offsets.unorderedAccess = 0;
		nvrhi::BindingLayoutDesc layout;
		layout.visibility = nvrhi::ShaderType::Pixel;
		layout.bindingOffsets = offsets;
		layout.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0));
		layout.addItem(nvrhi::BindingLayoutItem::Sampler(1));
		m_BindingLayout = m_Device->createBindingLayout(layout);
		return m_Sampler && m_BindingLayout;
	}

	void BlitPass::update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer)
	{
		if (m_Failed)
		{
			return;
		}
		if (m_Shaders->has_failed(engine_assets::BLIT_SHADER, ShaderStage::Vertex) || m_Shaders->has_failed(engine_assets::BLIT_SHADER, ShaderStage::Pixel))
		{
			m_Failed = true;
			return;
		}

		nvrhi::IShader* vertexShader = m_Shaders->get(engine_assets::BLIT_SHADER, ShaderStage::Vertex);
		nvrhi::IShader* pixelShader = m_Shaders->get(engine_assets::BLIT_SHADER, ShaderStage::Pixel);
		if (vertexShader == nullptr || pixelShader == nullptr || m_Shaders->get_generation() == m_BuiltGeneration)
		{
			return;
		}

		nvrhi::GraphicsPipelineDesc description;
		description.primType = nvrhi::PrimitiveType::TriangleList;
		description.VS = vertexShader;
		description.PS = pixelShader;
		description.addBindingLayout(m_BindingLayout);
		description.renderState.depthStencilState.depthTestEnable = false;
		description.renderState.depthStencilState.depthWriteEnable = false;
		description.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
		nvrhi::GraphicsPipelineHandle pipeline = device->createGraphicsPipeline(description, backbuffer);
		m_BuiltGeneration = m_Shaders->get_generation();
		if (pipeline == nullptr)
		{
			WI_BLIT_LOG(Error, "Could not create the blit pipeline");
			m_Failed = m_Pipeline == nullptr;
			return;
		}
		m_Pipeline = std::move(pipeline);
	}

	void BlitPass::render(nvrhi::ICommandList* commandList, const FrameDescription& frame)
	{
		if (m_Pipeline == nullptr || frame.SceneInViewport || frame.Scene == nullptr || !frame.Scene->active() || frame.Backbuffer == nullptr)
		{
			return;
		}

		nvrhi::ITexture* image = frame.Scene->viewport()->ldr();
		if (m_BindingSet == nullptr || m_BoundImage != image)
		{
			nvrhi::BindingSetDesc set;
			set.addItem(nvrhi::BindingSetItem::Texture_SRV(0, image));
			set.addItem(nvrhi::BindingSetItem::Sampler(1, m_Sampler));
			m_BindingSet = m_Device->createBindingSet(set, m_BindingLayout);
			m_BoundImage = image;
		}

		nvrhi::GraphicsState state;
		state.pipeline = m_Pipeline;
		state.framebuffer = frame.Backbuffer;
		state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<float>(frame.Width), static_cast<float>(frame.Height)));
		state.addBindingSet(m_BindingSet);
		commandList->setGraphicsState(state);

		nvrhi::DrawArguments arguments;
		arguments.vertexCount = 3;
		commandList->draw(arguments);
	}
}
