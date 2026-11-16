#include <pch.h>
#include "taa_pass.h"

#include "../view/view_state.h"

#include "../engine_assets.h"
#include "../scene/scene_render_data.h"

#define WI_TAA_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		constexpr u32 CONSTANT_BUFFER_VERSIONS = 4;
	}

	bool TaaPass::initialize(const PassInitializationDescription& description)
	{
		m_Device = description.Device;
		if (!m_Shaders.initialize(description, "TAA", { { engine_assets::TAA_SHADER, ShaderStage::Compute } }))
		{
			return false;
		}

		nvrhi::BufferDesc constants;
		constants.byteSize = sizeof(TaaData);
		constants.isConstantBuffer = true;
		constants.isVolatile = true;
		constants.maxVersions = CONSTANT_BUFFER_VERSIONS;
		constants.debugName = "TAA constants";
		m_ConstantBuffer = m_Device->createBuffer(constants);

		m_Sampler = m_Device->createSampler(nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));

		nvrhi::VulkanBindingOffsets offsets;
		offsets.shaderResource = 0;
		offsets.sampler = 0;
		offsets.constantBuffer = 0;
		offsets.unorderedAccess = 0;

		// Slots match engine/shaders/taa.hlsl: b0 constants, SRV 1 the image, 2 the depth, 3 the history, 4 the velocity, 5 the depth
		// history, sampler 6, UAV 7 the new history, 8 the new depth history.
		nvrhi::BindingLayoutDesc layout;
		layout.visibility = nvrhi::ShaderType::Compute;
		layout.bindingOffsets = offsets;
		layout.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0));
		for (u32 slot = 1; slot <= 5; slot++)
		{
			layout.addItem(nvrhi::BindingLayoutItem::Texture_SRV(slot));
		}
		layout.addItem(nvrhi::BindingLayoutItem::Sampler(6));
		layout.addItem(nvrhi::BindingLayoutItem::Texture_UAV(7));
		layout.addItem(nvrhi::BindingLayoutItem::Texture_UAV(8));
		m_BindingLayout = m_Device->createBindingLayout(layout);
		return m_ConstantBuffer && m_Sampler && m_BindingLayout;
	}

	bool TaaPass::is_ready() const
	{
		return m_Shaders.ready();
	}

	void TaaPass::update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer)
	{
		if (m_Shaders.failed())
		{
			m_Failed = true;
		}
	}

	bool TaaPass::create_binding_sets(ViewportTarget& viewport, ViewCache& c)
	{
		bool ok = true;
		for (u32 i = 0; i < 2; i++)
		{
			const u32 previous = 1 - i;
			nvrhi::BindingSetDesc set;
			set.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer));
			set.addItem(nvrhi::BindingSetItem::Texture_SRV(1, viewport.hdr()));
			set.addItem(nvrhi::BindingSetItem::Texture_SRV(2, viewport.depth()));
			set.addItem(nvrhi::BindingSetItem::Texture_SRV(3, viewport.taa_history(previous)));
			set.addItem(nvrhi::BindingSetItem::Texture_SRV(4, viewport.velocity()));
			set.addItem(nvrhi::BindingSetItem::Texture_SRV(5, viewport.taa_depth_history(previous)));
			set.addItem(nvrhi::BindingSetItem::Sampler(6, m_Sampler));
			set.addItem(nvrhi::BindingSetItem::Texture_UAV(7, viewport.taa_history(i)));
			set.addItem(nvrhi::BindingSetItem::Texture_UAV(8, viewport.taa_depth_history(i)));
			c.BindingSets[i] = m_Device->createBindingSet(set, m_BindingLayout);
			ok = ok && c.BindingSets[i] != nullptr;
		}
		return ok;
	}

	void TaaPass::render(nvrhi::ICommandList* commandList, const FrameDescription& frame)
	{
		ViewCache& c = m_PerView.get(frame.View->Target);
		if (frame.Scene == nullptr || !frame.Scene->active() || m_Failed || !frame.Scene->temporal_active())
		{
			return;
		}

		ViewportTarget& viewport = *frame.Scene->viewport();
		const bool shadersChanged = m_Shaders.take_generation_change();
		if (m_Pipeline == nullptr || shadersChanged)
		{
			nvrhi::ComputePipelineDesc pipeline;
			pipeline.CS = m_Shaders->get(engine_assets::TAA_SHADER, ShaderStage::Compute);
			pipeline.addBindingLayout(m_BindingLayout);
			m_Pipeline = m_Device->createComputePipeline(pipeline);
			if (m_Pipeline == nullptr)
			{
				WI_TAA_LOG(Error, "Could not create the TAA pipeline");
				m_Failed = true;
				return;
			}
		}

		if (c.BindingSets[0] == nullptr)
		{
			if (!create_binding_sets(viewport, c))
			{
				WI_TAA_LOG(Error, "Could not create the TAA bindings");
				m_Failed = true;
				return;
			}
		}

		// The history is the previous frame result only if this pass resolved the frame right before this one, on the same targets
		// (TAA may have been off in between: the textures then hold an old image).
		const bool consecutive = c.HasRun && c.LastRunFrame + 1 == frame.FrameIndex && c.LastRunGeneration == viewport.generation();
		const bool useHistory = consecutive && frame.Scene->history_valid();

		const FrameConstants& constants = frame.Scene->constants();
		TaaData data;
		data.InverseViewProjection = glm::inverse(constants.UnjitteredViewProjection);
		data.PreviousViewProjection = constants.PreviousViewProjection;
		data.Parameters = glm::vec4(frame.Scene->settings().Taa.Feedback, useHistory ? 1.0f : 0.0f, static_cast<float>(viewport.width()), static_cast<float>(viewport.height()));
		data.DepthInfo = glm::vec4(constants.CameraPosition.w, constants.CameraParams.x, constants.ClusterParams.z, 0.0f);
		commandList->writeBuffer(m_ConstantBuffer, &data, sizeof(data));

		nvrhi::ComputeState state;
		state.pipeline = m_Pipeline;
		state.addBindingSet(c.BindingSets[c.Current]);
		commandList->setComputeState(state);
		commandList->dispatch(ceil_div(viewport.width(), GROUP_SIZE), ceil_div(viewport.height(), GROUP_SIZE), 1);

		// Everything after this pass reads the resolved image from hdr(); the history keeps its own copy.
		commandList->copyTexture(viewport.hdr(), nvrhi::TextureSlice(), viewport.taa_history(c.Current), nvrhi::TextureSlice());

		c.HasRun = true;
		c.LastRunFrame = frame.FrameIndex;
		c.LastRunGeneration = viewport.generation();
		c.Current = 1 - c.Current;
	}
}
