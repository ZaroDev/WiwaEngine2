#include <pch.h>
#include "tonemap_pass.h"

#include "../engine_assets.h"
#include "../scene/scene_render_data.h"

#define WI_TONEMAP_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	bool TonemapPass::initialize(const PassInitializationDescription& description)
	{
		m_Device = description.Device;
		m_Shaders = description.Shaders;
		if (m_Shaders == nullptr)
		{
			WI_TONEMAP_LOG(Error, "The tonemap pass needs the shader library");
			return false;
		}

		m_Shaders->request(engine_assets::TONEMAP_SHADER, ShaderStage::Vertex);
		m_Shaders->request(engine_assets::TONEMAP_SHADER, ShaderStage::Pixel);

		const nvrhi::SamplerDesc samplerDescription = nvrhi::SamplerDesc().setAllFilters(false).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp);
		m_Sampler = m_Device->createSampler(samplerDescription);
		m_LinearSampler = m_Device->createSampler(nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));

		// Slots match engine/shaders/tonemap.hlsl: SRV 0, sampler 1, push constants 2, SRV 3 (the exposure), SRV 4 (the bloom), sampler 5.
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
		layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3));
		layout.addItem(nvrhi::BindingLayoutItem::Texture_SRV(4));
		layout.addItem(nvrhi::BindingLayoutItem::Sampler(5));
		m_BindingLayout = m_Device->createBindingLayout(layout);
		return m_Sampler && m_LinearSampler && m_BindingLayout;
	}

	bool TonemapPass::is_ready() const
	{
		return m_Shaders->get(engine_assets::TONEMAP_SHADER, ShaderStage::Vertex) != nullptr && m_Shaders->get(engine_assets::TONEMAP_SHADER, ShaderStage::Pixel) != nullptr;
	}

	void TonemapPass::update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer)
	{
		if (m_Shaders->has_failed(engine_assets::TONEMAP_SHADER, ShaderStage::Vertex) || m_Shaders->has_failed(engine_assets::TONEMAP_SHADER, ShaderStage::Pixel))
		{
			m_Failed = true;
		}
	}

	void TonemapPass::render(nvrhi::ICommandList* commandList, const FrameDescription& frame)
	{
		if (frame.Scene == nullptr || !frame.Scene->active() || m_Failed)
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
			description.VS = m_Shaders->get(engine_assets::TONEMAP_SHADER, ShaderStage::Vertex);
			description.PS = m_Shaders->get(engine_assets::TONEMAP_SHADER, ShaderStage::Pixel);
			description.addBindingLayout(m_BindingLayout);
			description.renderState.depthStencilState.depthTestEnable = false;
			description.renderState.depthStencilState.depthWriteEnable = false;
			description.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
			m_Pipeline = m_Device->createGraphicsPipeline(description, viewport.ldr_framebuffer());

			nvrhi::BindingSetDesc set;
			set.addItem(nvrhi::BindingSetItem::Texture_SRV(0, viewport.hdr()));
			set.addItem(nvrhi::BindingSetItem::Sampler(1, m_Sampler));
			set.addItem(nvrhi::BindingSetItem::PushConstants(2, sizeof(PushConstants)));
			set.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(3, viewport.exposure_buffer()));
			set.addItem(nvrhi::BindingSetItem::Texture_SRV(4, viewport.bloom()));
			set.addItem(nvrhi::BindingSetItem::Sampler(5, m_LinearSampler));
			m_BindingSet = m_Device->createBindingSet(set, m_BindingLayout);

			if (m_Pipeline == nullptr || m_BindingSet == nullptr)
			{
				WI_TONEMAP_LOG(Error, "Could not create the tonemap pipeline");
				m_Failed = true;
				return;
			}
		}

		nvrhi::GraphicsState state;
		state.pipeline = m_Pipeline;
		state.framebuffer = frame.Scene->quality().Fxaa ? viewport.pre_fxaa_framebuffer() : viewport.ldr_framebuffer(); // FXAA then writes the final image
		state.viewport.addViewportAndScissorRect(nvrhi::Viewport(static_cast<float>(viewport.width()), static_cast<float>(viewport.height())));
		state.addBindingSet(m_BindingSet);
		commandList->setGraphicsState(state);

		PushConstants constants;
		const SceneSettings& settings = frame.Scene->settings();
		constants.Exposure = std::exp2(settings.ExposureEv);
		constants.AutoExposure = settings.Exposure.Auto ? 1.0f : 0.0f;
		constants.BloomIntensity = settings.Bloom.Enabled ? settings.Bloom.Intensity : 0.0f;
		constants.Operator = static_cast<u32>(settings.Tonemap.Operator);
		constants.Contrast = settings.Tonemap.Contrast;
		constants.Saturation = settings.Tonemap.Saturation;
		constants.Temperature = settings.Tonemap.Temperature;
		constants.Tint = settings.Tonemap.Tint;
		constants.Vignette = settings.Tonemap.Vignette;
		commandList->setPushConstants(&constants, sizeof(constants));

		nvrhi::DrawArguments arguments;
		arguments.vertexCount = 3;
		commandList->draw(arguments);
	}
}
