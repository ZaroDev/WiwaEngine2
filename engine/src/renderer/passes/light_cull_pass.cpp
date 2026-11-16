#include <pch.h>
#include "light_cull_pass.h"

#include "../view/view_state.h"

#include "../engine_assets.h"
#include "../scene/scene_render_data.h"

#define WI_CULL_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	bool LightCullPass::initialize(const PassInitializationDescription& description)
	{
		m_Device = description.Device;
		if (!m_Shaders.initialize(description, "light culling", { { engine_assets::LIGHT_CULL_SHADER, ShaderStage::Compute } }))
		{
			return false;
		}

		// Slots match engine/shaders/light_cull.hlsl: b0 frame constants, SRV 3 lights, UAV 6 counts, UAV 7 indices.
		nvrhi::VulkanBindingOffsets offsets;
		offsets.shaderResource = 0;
		offsets.sampler = 0;
		offsets.constantBuffer = 0;
		offsets.unorderedAccess = 0;
		nvrhi::BindingLayoutDesc layout;
		layout.visibility = nvrhi::ShaderType::Compute;
		layout.bindingOffsets = offsets;
		layout.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0));
		layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(3));
		layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(6));
		layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(7));
		m_Layout = m_Device->createBindingLayout(layout);
		return m_Layout != nullptr;
	}

	bool LightCullPass::is_ready() const
	{
		return m_Shaders.ready();
	}

	void LightCullPass::update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer)
	{
		if (m_Shaders.failed())
		{
			m_Failed = true;
		}
	}

	void LightCullPass::render(nvrhi::ICommandList* commandList, const FrameDescription& frame)
	{
		if (frame.Scene == nullptr || !frame.Scene->active() || !frame.Scene->has_camera() || frame.Scene->lights().empty() || m_Failed)
		{
			return;
		}

		const SceneRenderData& data = *frame.Scene;
		const bool shadersChanged = m_Shaders.take_generation_change();
		if (m_Pipeline == nullptr || shadersChanged)
		{
			nvrhi::ComputePipelineDesc description;
			description.CS = m_Shaders->get(engine_assets::LIGHT_CULL_SHADER, ShaderStage::Compute);
			description.addBindingLayout(m_Layout);
			m_Pipeline = m_Device->createComputePipeline(description);
			if (m_Pipeline == nullptr)
			{
				WI_CULL_LOG(Error, "Could not create the light culling pipeline");
				m_Failed = true;
				return;
			}
		}

		ViewCache& cache = m_PerView.get(frame.View->Target);
		if (cache.BindingSet == nullptr)
		{
			nvrhi::BindingSetDesc set;
			set.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, data.constant_buffer()));
			set.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(3, data.light_buffer()));
			set.addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(6, data.cluster_counts()));
			set.addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(7, data.cluster_indices()));
			cache.BindingSet = m_Device->createBindingSet(set, m_Layout);
			if (cache.BindingSet == nullptr)
			{
				WI_CULL_LOG(Error, "Could not create the light culling bindings");
				m_Failed = true;
				return;
			}
		}

		nvrhi::ComputeState state;
		state.pipeline = m_Pipeline;
		state.addBindingSet(cache.BindingSet);
		commandList->setComputeState(state);
		commandList->dispatch(ceil_div(CLUSTERS_X, LIGHT_CULL_GROUP_X), ceil_div(CLUSTERS_Y, LIGHT_CULL_GROUP_Y), ceil_div(CLUSTERS_Z, LIGHT_CULL_GROUP_Z));
	}
}
