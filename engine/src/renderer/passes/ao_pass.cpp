#include <pch.h>
#include "ao_pass.h"

#include <algorithm>

#include "../engine_assets.h"
#include "../scene/scene_render_data.h"

#define WI_AO_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		constexpr u32 CONSTANT_BUFFER_VERSIONS = 4;
		// Clip-space Y points up in the projections of this engine (glm::perspectiveRH_ZO); gtao.hlsl takes the sign as a constant.
		constexpr float CLIP_SPACE_Y_SIGN = 1.0f;
	}

	bool AoPass::initialize(const PassInitializationDescription& description)
	{
		m_Device = description.Device;
		m_Shaders = description.Shaders;
		if (m_Shaders == nullptr)
		{
			WI_AO_LOG(Error, "The AO pass needs the shader library");
			return false;
		}

		m_Shaders->request(engine_assets::GTAO_DEPTH_SHADER, ShaderStage::Compute);
		m_Shaders->request(engine_assets::GTAO_SHADER, ShaderStage::Compute);
		m_Shaders->request(engine_assets::GTAO_BLUR_SHADER, ShaderStage::Compute);

		nvrhi::BufferDesc constants;
		constants.byteSize = sizeof(AoData);
		constants.isConstantBuffer = true;
		constants.isVolatile = true;
		constants.maxVersions = CONSTANT_BUFFER_VERSIONS;
		constants.debugName = "AO constants";
		m_ConstantBuffer = m_Device->createBuffer(constants);

		nvrhi::VulkanBindingOffsets offsets;
		offsets.shaderResource = 0;
		offsets.sampler = 0;
		offsets.constantBuffer = 0;
		offsets.unorderedAccess = 0;

		// Slots match engine/shaders/gtao_depth.hlsl: b0 constants, SRV 1 the depth, UAV 3 to 7 the levels of the distances.
		nvrhi::BindingLayoutDesc depth;
		depth.visibility = nvrhi::ShaderType::Compute;
		depth.bindingOffsets = offsets;
		depth.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0));
		depth.addItem(nvrhi::BindingLayoutItem::Texture_SRV(1));
		for (u32 level = 0; level < ViewportTarget::MAX_AO_DEPTH_LEVELS; level++)
		{
			depth.addItem(nvrhi::BindingLayoutItem::Texture_UAV(3 + level));
		}
		m_DepthLayout = m_Device->createBindingLayout(depth);

		// gtao.hlsl: b0, SRV 1 the distances, SRV 2 the normals, UAV 3 the raw occlusion.
		nvrhi::BindingLayoutDesc ao;
		ao.visibility = nvrhi::ShaderType::Compute;
		ao.bindingOffsets = offsets;
		ao.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0));
		ao.addItem(nvrhi::BindingLayoutItem::Texture_SRV(1));
		ao.addItem(nvrhi::BindingLayoutItem::Texture_SRV(2));
		ao.addItem(nvrhi::BindingLayoutItem::Texture_UAV(3));
		m_AoLayout = m_Device->createBindingLayout(ao);

		// gtao_blur.hlsl: b0 (the far plane finds the sky), SRV 1 the distances, SRV 2 the raw occlusion, UAV 3 the blurred one.
		nvrhi::BindingLayoutDesc blur;
		blur.visibility = nvrhi::ShaderType::Compute;
		blur.bindingOffsets = offsets;
		blur.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0));
		blur.addItem(nvrhi::BindingLayoutItem::Texture_SRV(1));
		blur.addItem(nvrhi::BindingLayoutItem::Texture_SRV(2));
		blur.addItem(nvrhi::BindingLayoutItem::Texture_UAV(3));
		m_BlurLayout = m_Device->createBindingLayout(blur);
		return m_ConstantBuffer && m_DepthLayout && m_AoLayout && m_BlurLayout;
	}

	bool AoPass::is_ready() const
	{
		return m_Shaders->get(engine_assets::GTAO_DEPTH_SHADER, ShaderStage::Compute) != nullptr
			&& m_Shaders->get(engine_assets::GTAO_SHADER, ShaderStage::Compute) != nullptr
			&& m_Shaders->get(engine_assets::GTAO_BLUR_SHADER, ShaderStage::Compute) != nullptr;
	}

	void AoPass::update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer)
	{
		if (m_Shaders->has_failed(engine_assets::GTAO_DEPTH_SHADER, ShaderStage::Compute) || m_Shaders->has_failed(engine_assets::GTAO_SHADER, ShaderStage::Compute)
			|| m_Shaders->has_failed(engine_assets::GTAO_BLUR_SHADER, ShaderStage::Compute))
		{
			m_Failed = true;
		}
	}

	bool AoPass::create_binding_sets(ViewportTarget& viewport)
	{
		nvrhi::BindingSetDesc depth;
		depth.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer));
		depth.addItem(nvrhi::BindingSetItem::Texture_SRV(1, viewport.depth()));
		for (u32 level = 0; level < ViewportTarget::MAX_AO_DEPTH_LEVELS; level++)
		{
			// The levels past the last one the texture has are bound to the last one (the shader does not write them).
			const u32 mip = std::min(level, viewport.ao_depth_levels() - 1);
			depth.addItem(nvrhi::BindingSetItem::Texture_UAV(3 + level, viewport.ao_distance(), nvrhi::Format::UNKNOWN, nvrhi::TextureSubresourceSet(mip, 1, 0, 1)));
		}
		m_DepthSet = m_Device->createBindingSet(depth, m_DepthLayout);

		nvrhi::BindingSetDesc ao;
		ao.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer));
		ao.addItem(nvrhi::BindingSetItem::Texture_SRV(1, viewport.ao_distance()));
		ao.addItem(nvrhi::BindingSetItem::Texture_SRV(2, viewport.normal()));
		ao.addItem(nvrhi::BindingSetItem::Texture_UAV(3, viewport.ao_raw()));
		m_AoSet = m_Device->createBindingSet(ao, m_AoLayout);

		nvrhi::BindingSetDesc blur;
		blur.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_ConstantBuffer));
		blur.addItem(nvrhi::BindingSetItem::Texture_SRV(1, viewport.ao_distance()));
		blur.addItem(nvrhi::BindingSetItem::Texture_SRV(2, viewport.ao_raw()));
		blur.addItem(nvrhi::BindingSetItem::Texture_UAV(3, viewport.ao()));
		m_BlurSet = m_Device->createBindingSet(blur, m_BlurLayout);
		return m_DepthSet && m_AoSet && m_BlurSet;
	}

	void AoPass::render(nvrhi::ICommandList* commandList, const FrameDescription& frame)
	{
		if (frame.Scene == nullptr || !frame.Scene->active() || m_Failed || !frame.Scene->ao_active())
		{
			return;
		}

		ViewportTarget& viewport = *frame.Scene->viewport();
		const u32 shaderGeneration = m_Shaders->get_generation();
		if (m_DepthPipeline == nullptr || shaderGeneration != m_BuiltShaderGeneration)
		{
			m_BuiltShaderGeneration = shaderGeneration;
			nvrhi::ComputePipelineDesc depth;
			depth.CS = m_Shaders->get(engine_assets::GTAO_DEPTH_SHADER, ShaderStage::Compute);
			depth.addBindingLayout(m_DepthLayout);
			m_DepthPipeline = m_Device->createComputePipeline(depth);

			nvrhi::ComputePipelineDesc ao;
			ao.CS = m_Shaders->get(engine_assets::GTAO_SHADER, ShaderStage::Compute);
			ao.addBindingLayout(m_AoLayout);
			m_AoPipeline = m_Device->createComputePipeline(ao);

			nvrhi::ComputePipelineDesc blur;
			blur.CS = m_Shaders->get(engine_assets::GTAO_BLUR_SHADER, ShaderStage::Compute);
			blur.addBindingLayout(m_BlurLayout);
			m_BlurPipeline = m_Device->createComputePipeline(blur);
			if (m_DepthPipeline == nullptr || m_AoPipeline == nullptr || m_BlurPipeline == nullptr)
			{
				WI_AO_LOG(Error, "Could not create the AO pipelines");
				m_Failed = true;
				return;
			}
		}

		if (m_DepthSet == nullptr || viewport.generation() != m_BuiltViewportGeneration)
		{
			m_BuiltViewportGeneration = viewport.generation();
			if (!create_binding_sets(viewport))
			{
				WI_AO_LOG(Error, "Could not create the AO bindings");
				m_Failed = true;
				return;
			}
		}

		const FrameConstants& constants = frame.Scene->constants();
		const AoSettings& settings = frame.Scene->settings().Ao;
		const RenderQuality& quality = frame.Scene->quality();
		AoData data;
		data.View = constants.View;
		data.Projection = glm::vec4(constants.Projection[0][0], constants.Projection[1][1], CLIP_SPACE_Y_SIGN, static_cast<float>(viewport.ao_depth_levels() - 1));
		data.DepthInfo = glm::vec4(constants.CameraPosition.w, constants.CameraParams.x, static_cast<float>(viewport.width()), static_cast<float>(viewport.height()));
		data.Settings = glm::vec4(settings.Radius, settings.Power, static_cast<float>(quality.AoSlices), static_cast<float>(quality.AoSteps));
		commandList->writeBuffer(m_ConstantBuffer, &data, sizeof(data));

		const u32 groupsX = (viewport.width() + GROUP_SIZE - 1) / GROUP_SIZE;
		const u32 groupsY = (viewport.height() + GROUP_SIZE - 1) / GROUP_SIZE;

		// The distance of every pixel with its mips: one dispatch.
		nvrhi::ComputeState depthState;
		depthState.pipeline = m_DepthPipeline;
		depthState.addBindingSet(m_DepthSet);
		commandList->setComputeState(depthState);
		commandList->dispatch((viewport.width() + DEPTH_GROUP_SIZE - 1) / DEPTH_GROUP_SIZE, (viewport.height() + DEPTH_GROUP_SIZE - 1) / DEPTH_GROUP_SIZE, 1);

		nvrhi::ComputeState aoState;
		aoState.pipeline = m_AoPipeline;
		aoState.addBindingSet(m_AoSet);
		commandList->setComputeState(aoState);
		commandList->dispatch(groupsX, groupsY, 1);

		nvrhi::ComputeState blurState;
		blurState.pipeline = m_BlurPipeline;
		blurState.addBindingSet(m_BlurSet);
		commandList->setComputeState(blurState);
		commandList->dispatch(groupsX, groupsY, 1);
	}
}
