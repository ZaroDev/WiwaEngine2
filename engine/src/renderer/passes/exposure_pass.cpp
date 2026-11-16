#include <pch.h>
#include "exposure_pass.h"

#include "../view/view_state.h"

#include <algorithm>

#include "../engine_assets.h"
#include "../scene/scene_render_data.h"

#define WI_EXPOSURE_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	bool ExposurePass::initialize(const PassInitializationDescription& description)
	{
		m_Device = description.Device;
		m_Shaders = description.Shaders;
		if (m_Shaders == nullptr)
		{
			WI_EXPOSURE_LOG(Error, "The exposure pass needs the shader library");
			return false;
		}

		m_Shaders->request(engine_assets::EXPOSURE_HISTOGRAM_SHADER, ShaderStage::Compute);
		m_Shaders->request(engine_assets::EXPOSURE_RESOLVE_SHADER, ShaderStage::Compute);

		nvrhi::VulkanBindingOffsets offsets;
		offsets.shaderResource = 0;
		offsets.sampler = 0;
		offsets.constantBuffer = 0;
		offsets.unorderedAccess = 0;

		// Slots match engine/shaders/exposure_histogram.hlsl: SRV 0 the HDR color, UAV 1 the histogram, push constants 2.
		nvrhi::BindingLayoutDesc histogram;
		histogram.visibility = nvrhi::ShaderType::Compute;
		histogram.bindingOffsets = offsets;
		histogram.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0));
		histogram.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(1));
		histogram.addItem(nvrhi::BindingLayoutItem::PushConstants(2, sizeof(HistogramConstants)));
		m_HistogramLayout = m_Device->createBindingLayout(histogram);

		// Slots match engine/shaders/exposure_resolve.hlsl: UAV 0 the histogram, UAV 1 the exposure, push constants 2.
		nvrhi::BindingLayoutDesc resolve;
		resolve.visibility = nvrhi::ShaderType::Compute;
		resolve.bindingOffsets = offsets;
		resolve.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0));
		resolve.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(1));
		resolve.addItem(nvrhi::BindingLayoutItem::PushConstants(2, sizeof(ResolveConstants)));
		m_ResolveLayout = m_Device->createBindingLayout(resolve);
		return m_HistogramLayout && m_ResolveLayout;
	}

	bool ExposurePass::is_ready() const
	{
		return m_Shaders->get(engine_assets::EXPOSURE_HISTOGRAM_SHADER, ShaderStage::Compute) != nullptr
			&& m_Shaders->get(engine_assets::EXPOSURE_RESOLVE_SHADER, ShaderStage::Compute) != nullptr;
	}

	void ExposurePass::update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer)
	{
		if (m_Shaders->has_failed(engine_assets::EXPOSURE_HISTOGRAM_SHADER, ShaderStage::Compute) || m_Shaders->has_failed(engine_assets::EXPOSURE_RESOLVE_SHADER, ShaderStage::Compute))
		{
			m_Failed = true;
		}
	}

	void ExposurePass::render(nvrhi::ICommandList* commandList, const FrameDescription& frame)
	{
		ViewCache& c = m_PerView.get(frame.View->Target);
		if (frame.Scene == nullptr || !frame.Scene->active() || m_Failed)
		{
			c.WasActive = false;
			return;
		}

		const ExposureSettings& settings = frame.Scene->settings().Exposure;
		if (!settings.Auto)
		{
			c.WasActive = false; // the next time it is on there is no history to adapt from
			return;
		}

		ViewportTarget& viewport = *frame.Scene->viewport();
		const u32 shaderGeneration = m_Shaders->get_generation();
		if (m_HistogramPipeline == nullptr || shaderGeneration != m_BuiltShaderGeneration)
		{
			m_BuiltShaderGeneration = shaderGeneration;
			nvrhi::ComputePipelineDesc histogram;
			histogram.CS = m_Shaders->get(engine_assets::EXPOSURE_HISTOGRAM_SHADER, ShaderStage::Compute);
			histogram.addBindingLayout(m_HistogramLayout);
			m_HistogramPipeline = m_Device->createComputePipeline(histogram);

			nvrhi::ComputePipelineDesc resolve;
			resolve.CS = m_Shaders->get(engine_assets::EXPOSURE_RESOLVE_SHADER, ShaderStage::Compute);
			resolve.addBindingLayout(m_ResolveLayout);
			m_ResolvePipeline = m_Device->createComputePipeline(resolve);
			if (m_HistogramPipeline == nullptr || m_ResolvePipeline == nullptr)
			{
				WI_EXPOSURE_LOG(Error, "Could not create the exposure pipelines");
				m_Failed = true;
				return;
			}
		}

		if (c.HistogramSet == nullptr)
		{
			nvrhi::BindingSetDesc histogram;
			histogram.addItem(nvrhi::BindingSetItem::Texture_SRV(0, viewport.hdr()));
			histogram.addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(1, viewport.histogram_buffer()));
			histogram.addItem(nvrhi::BindingSetItem::PushConstants(2, sizeof(HistogramConstants)));
			c.HistogramSet = m_Device->createBindingSet(histogram, m_HistogramLayout);

			nvrhi::BindingSetDesc resolve;
			resolve.addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(0, viewport.histogram_buffer()));
			resolve.addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(1, viewport.exposure_buffer()));
			resolve.addItem(nvrhi::BindingSetItem::PushConstants(2, sizeof(ResolveConstants)));
			c.ResolveSet = m_Device->createBindingSet(resolve, m_ResolveLayout);
			if (c.HistogramSet == nullptr || c.ResolveSet == nullptr)
			{
				WI_EXPOSURE_LOG(Error, "Could not create the exposure bindings");
				m_Failed = true;
				return;
			}
			c.WasActive = false; // new buffers: their contents are not defined
		}

		const u32 sceneResets = frame.Scene->scene() != nullptr ? frame.Scene->scene()->reset_count() : 0;
		if (sceneResets != c.LastSceneResets)
		{
			c.LastSceneResets = sceneResets;
			c.WasActive = false; // another scene: what the old one measured says nothing about this one
		}

		const bool reset = !c.WasActive;
		if (reset)
		{
			commandList->clearBufferUInt(viewport.exposure_buffer(), 0);
			commandList->clearBufferUInt(viewport.histogram_buffer(), 0);
		}

		ResolveConstants resolve;
		resolve.LowShare = settings.LowPercent;
		resolve.HighShare = std::max(settings.HighPercent, settings.LowPercent);
		resolve.Key = settings.Key;
		resolve.CompensationEv = settings.CompensationEv;
		resolve.MinEv = settings.MinEv;
		resolve.MaxEv = std::max(settings.MaxEv, settings.MinEv);
		resolve.DeltaSeconds = std::clamp(frame.DeltaSeconds, 0.0f, MAX_DELTA);
		resolve.Speed = settings.AdaptSpeed;
		resolve.Reset = reset ? 1.0f : 0.0f;
		c.WasActive = true;

		const HistogramConstants histogram;
		{
			nvrhi::ComputeState state;
			state.pipeline = m_HistogramPipeline;
			state.addBindingSet(c.HistogramSet);
			commandList->setComputeState(state);
			commandList->setPushConstants(&histogram, sizeof(histogram));
			commandList->dispatch(ceil_div(viewport.width(), EXPOSURE_GROUP_SIZE), ceil_div(viewport.height(), EXPOSURE_GROUP_SIZE), 1);
		}
		{
			nvrhi::ComputeState state;
			state.pipeline = m_ResolvePipeline;
			state.addBindingSet(c.ResolveSet);
			commandList->setComputeState(state);
			commandList->setPushConstants(&resolve, sizeof(resolve));
			commandList->dispatch(1, 1, 1);
		}
	}
}
