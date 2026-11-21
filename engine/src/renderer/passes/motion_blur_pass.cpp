#include <pch.h>
#include "motion_blur_pass.h"

#include "../view/view_state.h"

#include "../engine_assets.h"
#include "../scene/scene_render_data.h"

#define WI_MOTION_BLUR_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		constexpr u32 CONSTANT_BUFFER_VERSIONS = 4;
	}

	bool MotionBlurPass::initialize(const PassInitializationDescription& description)
	{
		m_Device = description.Device;
		if (!m_Shaders.initialize(description, "motion blur", { { engine_assets::MOTION_BLUR_SHADER, ShaderStage::Compute } }))
		{
			return false;
		}

		nvrhi::BufferDesc constants;
		constants.byteSize = sizeof(MotionBlurData);
		constants.isConstantBuffer = true;
		constants.isVolatile = true;
		constants.maxVersions = CONSTANT_BUFFER_VERSIONS;
		constants.debugName = "Motion blur constants";
		m_ConstantBuffer = m_Device->createBuffer(constants);

		m_Sampler = m_Device->createSampler(nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));

		nvrhi::VulkanBindingOffsets offsets;
		offsets.shaderResource = 0;
		offsets.sampler = 0;
		offsets.constantBuffer = 0;
		offsets.unorderedAccess = 0;

		// Slots match engine/shaders/motion_blur.hlsl: b0 constants, SRV 1 the image, 2 the velocity, 3 the depth, sampler 4, UAV 5 the result.
		nvrhi::BindingLayoutDesc layout;
		layout.visibility = nvrhi::ShaderType::Compute;
		layout.bindingOffsets = offsets;
		layout.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0));
		layout.addItem(nvrhi::BindingLayoutItem::Texture_SRV(1));
		layout.addItem(nvrhi::BindingLayoutItem::Texture_SRV(2));
		layout.addItem(nvrhi::BindingLayoutItem::Texture_SRV(3));
		layout.addItem(nvrhi::BindingLayoutItem::Sampler(4));
		layout.addItem(nvrhi::BindingLayoutItem::Texture_UAV(5));
		m_BindingLayout = m_Device->createBindingLayout(layout);
		return m_ConstantBuffer && m_Sampler && m_BindingLayout;
	}

	bool MotionBlurPass::is_ready() const
	{
		return m_Shaders.ready();
	}

	void MotionBlurPass::update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer)
	{
		if (m_Shaders.failed())
		{
			m_Failed = true;
		}
	}

	bool MotionBlurPass::create_binding_set(ViewportTarget& viewport, ViewCache& c)
	{
		nvrhi::BindingSetDesc set;
		set.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer));
		set.addItem(nvrhi::BindingSetItem::Texture_SRV(1, viewport.hdr()));
		set.addItem(nvrhi::BindingSetItem::Texture_SRV(2, viewport.velocity()));
		set.addItem(nvrhi::BindingSetItem::Texture_SRV(3, viewport.depth()));
		set.addItem(nvrhi::BindingSetItem::Sampler(4, m_Sampler));
		set.addItem(nvrhi::BindingSetItem::Texture_UAV(5, viewport.post_scratch()));
		c.BindingSet = m_Device->createBindingSet(set, m_BindingLayout);
		return c.BindingSet != nullptr;
	}

	void MotionBlurPass::render(nvrhi::ICommandList* commandList, const FrameDescription& frame)
	{
		ViewCache& c = m_PerView.get(frame.View->Target);
		if (frame.Scene == nullptr || !frame.Scene->active() || m_Failed || !frame.Scene->has_camera())
		{
			return;
		}

		const MotionBlurSettings& settings = frame.Scene->settings().MotionBlur;
		const bool debug = frame.Scene->debug_velocity();
		if (!settings.Enabled && !debug)
		{
			return;
		}

		ViewportTarget& viewport = *frame.Scene->viewport();
		const bool shadersChanged = m_Shaders.take_generation_change();
		if (m_Pipeline == nullptr || shadersChanged)
		{
			nvrhi::ComputePipelineDesc pipeline;
			pipeline.CS = m_Shaders->get(engine_assets::MOTION_BLUR_SHADER, ShaderStage::Compute);
			pipeline.addBindingLayout(m_BindingLayout);
			m_Pipeline = m_Device->createComputePipeline(pipeline);
			if (m_Pipeline == nullptr)
			{
				WI_MOTION_BLUR_LOG(Error, "Could not create the motion blur pipeline");
				m_Failed = true;
				return;
			}
		}

		if (viewport.post_scratch() == nullptr)
		{
			return;
		}
		if (c.TargetGeneration != viewport.post_scratch_generation())
		{
			c.BindingSet = nullptr;
			c.TargetGeneration = viewport.post_scratch_generation();
		}
		if (c.BindingSet == nullptr)
		{
			if (!create_binding_set(viewport, c))
			{
				WI_MOTION_BLUR_LOG(Error, "Could not create the motion blur bindings");
				m_Failed = true;
				return;
			}
		}

		const FrameConstants& constants = frame.Scene->constants();
		MotionBlurData data;
		data.InverseViewProjection = glm::inverse(constants.UnjitteredViewProjection);
		data.PreviousViewProjection = constants.PreviousViewProjection; // the current one without a history: a camera cut does not blur
		data.Parameters = glm::vec4(settings.ExposureTime / std::max(frame.DeltaSeconds, 0.001f) * settings.Strength, settings.MaxBlurPixels, static_cast<float>(viewport.width()), static_cast<float>(viewport.height()));
		data.Mode = glm::vec4(debug ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
		commandList->writeBuffer(m_ConstantBuffer, &data, sizeof(data));

		nvrhi::ComputeState state;
		state.pipeline = m_Pipeline;
		state.addBindingSet(c.BindingSet);
		commandList->setComputeState(state);
		commandList->dispatch(ceil_div(viewport.width(), GROUP_SIZE), ceil_div(viewport.height(), GROUP_SIZE), 1);

		commandList->copyTexture(viewport.hdr(), nvrhi::TextureSlice(), viewport.post_scratch(), nvrhi::TextureSlice());
	}
}
