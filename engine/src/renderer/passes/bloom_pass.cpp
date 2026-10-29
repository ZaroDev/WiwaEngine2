#include <pch.h>
#include "bloom_pass.h"

#include "../engine_assets.h"
#include "../scene/scene_render_data.h"
#include "bloom_math.h"

#define WI_BLOOM_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	bool BloomPass::initialize(const PassInitializationDescription& description)
	{
		m_Device = description.Device;
		m_Shaders = description.Shaders;
		if (m_Shaders == nullptr)
		{
			WI_BLOOM_LOG(Error, "The bloom pass needs the shader library");
			return false;
		}

		m_Shaders->request(engine_assets::BLOOM_DOWN_SHADER, ShaderStage::Compute);
		m_Shaders->request(engine_assets::BLOOM_UP_SHADER, ShaderStage::Compute);

		m_Sampler = m_Device->createSampler(nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp));

		nvrhi::VulkanBindingOffsets offsets;
		offsets.shaderResource = 0;
		offsets.sampler = 0;
		offsets.constantBuffer = 0;
		offsets.unorderedAccess = 0;

		// Slots match engine/shaders/bloom_down.hlsl: push constants 0, SRV 1 the source, UAV 2 the destination, sampler 3.
		nvrhi::BindingLayoutDesc down;
		down.visibility = nvrhi::ShaderType::Compute;
		down.bindingOffsets = offsets;
		down.addItem(nvrhi::BindingLayoutItem::PushConstants(0, sizeof(DownConstants)));
		down.addItem(nvrhi::BindingLayoutItem::Texture_SRV(1));
		down.addItem(nvrhi::BindingLayoutItem::Texture_UAV(2));
		down.addItem(nvrhi::BindingLayoutItem::Sampler(3));
		m_DownLayout = m_Device->createBindingLayout(down);

		// Slots match engine/shaders/bloom_up.hlsl: SRV 1 the smaller level, SRV 2 this level as the downsample made it, UAV 3 the
		// destination, sampler 4.
		nvrhi::BindingLayoutDesc up;
		up.visibility = nvrhi::ShaderType::Compute;
		up.bindingOffsets = offsets;
		up.addItem(nvrhi::BindingLayoutItem::Texture_SRV(1));
		up.addItem(nvrhi::BindingLayoutItem::Texture_SRV(2));
		up.addItem(nvrhi::BindingLayoutItem::Texture_UAV(3));
		up.addItem(nvrhi::BindingLayoutItem::Sampler(4));
		m_UpLayout = m_Device->createBindingLayout(up);
		return m_Sampler && m_DownLayout && m_UpLayout;
	}

	bool BloomPass::is_ready() const
	{
		return m_Shaders->get(engine_assets::BLOOM_DOWN_SHADER, ShaderStage::Compute) != nullptr
			&& m_Shaders->get(engine_assets::BLOOM_UP_SHADER, ShaderStage::Compute) != nullptr;
	}

	void BloomPass::update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer)
	{
		if (m_Shaders->has_failed(engine_assets::BLOOM_DOWN_SHADER, ShaderStage::Compute) || m_Shaders->has_failed(engine_assets::BLOOM_UP_SHADER, ShaderStage::Compute))
		{
			m_Failed = true;
		}
	}

	bool BloomPass::create_binding_sets(ViewportTarget& viewport)
	{
		bool ok = true;
		for (u32 level = 0; level < ViewportTarget::MAX_BLOOM_MIPS; level++)
		{
			nvrhi::ITexture* source = level == 0 ? viewport.hdr() : viewport.bloom_down(level - 1);
			nvrhi::BindingSetDesc set;
			set.addItem(nvrhi::BindingSetItem::PushConstants(0, sizeof(DownConstants)));
			set.addItem(nvrhi::BindingSetItem::Texture_SRV(1, source));
			set.addItem(nvrhi::BindingSetItem::Texture_UAV(2, viewport.bloom_down(level)));
			set.addItem(nvrhi::BindingSetItem::Sampler(3, m_Sampler));
			m_DownSets[level] = m_Device->createBindingSet(set, m_DownLayout);
			ok = ok && m_DownSets[level] != nullptr;
		}

		for (u32 level = 0; level + 1 < ViewportTarget::MAX_BLOOM_MIPS; level++)
		{
			// Two ways to reach up(level): from up(level + 1), or, for the deepest step, from down(level + 1). The deepest level has
			// no up image of its own, so the last step of a full chain can only be the deepest.
			for (const bool deepest : { false, true })
			{
				nvrhi::ITexture* smaller = deepest ? viewport.bloom_down(level + 1) : viewport.bloom_up(level + 1);
				if (smaller == nullptr)
				{
					continue;
				}
				nvrhi::BindingSetDesc set;
				set.addItem(nvrhi::BindingSetItem::Texture_SRV(1, smaller));
				set.addItem(nvrhi::BindingSetItem::Texture_SRV(2, viewport.bloom_down(level)));
				set.addItem(nvrhi::BindingSetItem::Texture_UAV(3, viewport.bloom_up(level)));
				set.addItem(nvrhi::BindingSetItem::Sampler(4, m_Sampler));
				nvrhi::BindingSetHandle& slot = deepest ? m_UpDeepestSets[level] : m_UpSets[level];
				slot = m_Device->createBindingSet(set, m_UpLayout);
				ok = ok && slot != nullptr;
			}
		}
		return ok;
	}

	void BloomPass::render(nvrhi::ICommandList* commandList, const FrameDescription& frame)
	{
		if (frame.Scene == nullptr || !frame.Scene->active() || m_Failed)
		{
			return;
		}

		const BloomSettings& settings = frame.Scene->settings().Bloom;
		if (!settings.Enabled || settings.Intensity <= 0.0f)
		{
			return; // the tonemap pass is told, and does not read the result
		}

		ViewportTarget& viewport = *frame.Scene->viewport();
		const u32 shaderGeneration = m_Shaders->get_generation();
		if (m_DownPipeline == nullptr || shaderGeneration != m_BuiltShaderGeneration)
		{
			m_BuiltShaderGeneration = shaderGeneration;
			nvrhi::ComputePipelineDesc down;
			down.CS = m_Shaders->get(engine_assets::BLOOM_DOWN_SHADER, ShaderStage::Compute);
			down.addBindingLayout(m_DownLayout);
			m_DownPipeline = m_Device->createComputePipeline(down);

			nvrhi::ComputePipelineDesc up;
			up.CS = m_Shaders->get(engine_assets::BLOOM_UP_SHADER, ShaderStage::Compute);
			up.addBindingLayout(m_UpLayout);
			m_UpPipeline = m_Device->createComputePipeline(up);
			if (m_DownPipeline == nullptr || m_UpPipeline == nullptr)
			{
				WI_BLOOM_LOG(Error, "Could not create the bloom pipelines");
				m_Failed = true;
				return;
			}
		}

		if (viewport.generation() != m_BuiltViewportGeneration)
		{
			m_BuiltViewportGeneration = viewport.generation();
			if (!create_binding_sets(viewport))
			{
				WI_BLOOM_LOG(Error, "Could not create the bloom bindings");
				m_Failed = true;
				return;
			}
		}

		const u32 count = bloom_mip_count(frame.Scene->quality().BloomMips, viewport.width(), viewport.height());

		// Down: scene -> level 0 (with the threshold) -> level 1 -> ... -> level count - 1.
		for (u32 level = 0; level < count; level++)
		{
			const nvrhi::TextureDesc& source = (level == 0 ? viewport.hdr() : viewport.bloom_down(level - 1))->getDesc();
			DownConstants constants;
			constants.Threshold = settings.Threshold;
			constants.Knee = settings.Knee;
			constants.FirstStep = level == 0 ? 1.0f : 0.0f;
			constants.TexelX = 1.0f / static_cast<float>(source.width);
			constants.TexelY = 1.0f / static_cast<float>(source.height);

			nvrhi::ComputeState state;
			state.pipeline = m_DownPipeline;
			state.addBindingSet(m_DownSets[level]);
			commandList->setComputeState(state);
			commandList->setPushConstants(&constants, sizeof(constants));
			commandList->dispatch((viewport.bloom_width(level) + GROUP_SIZE - 1) / GROUP_SIZE, (viewport.bloom_height(level) + GROUP_SIZE - 1) / GROUP_SIZE, 1);
		}

		// Up: the deepest level spreads into the one above it, and so on until level 0, which is the finished bloom.
		for (u32 level = count - 1; level-- > 0;)
		{
			nvrhi::ComputeState state;
			state.pipeline = m_UpPipeline;
			state.addBindingSet(level == count - 2 ? m_UpDeepestSets[level] : m_UpSets[level]);
			commandList->setComputeState(state);
			commandList->dispatch((viewport.bloom_width(level) + GROUP_SIZE - 1) / GROUP_SIZE, (viewport.bloom_height(level) + GROUP_SIZE - 1) / GROUP_SIZE, 1);
		}
	}
}
