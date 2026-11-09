#include <pch.h>
#include "decal_pass.h"

#include <algorithm>
#include <cmath>

#include "../engine_assets.h"
#include "../scene/scene_render_data.h"
#include "../view/view_state.h"

#define WI_DECAL_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		// Mirrors DrawConstantsData of scene_common.hlsli: InstanceIndex is the decal's place in the records, MaterialSlot its material.
		struct DrawConstants
		{
			u32 InstanceIndex;
			u32 MaterialSlot;
			u32 BaseVertex;
		};

		constexpr u32 CUBE_VERTICES = 36;

		nvrhi::BlendState::RenderTarget premultiplied()
		{
			nvrhi::BlendState::RenderTarget blend;
			blend.blendEnable = true;
			blend.srcBlend = nvrhi::BlendFactor::One;
			blend.destBlend = nvrhi::BlendFactor::InvSrcAlpha;
			blend.blendOp = nvrhi::BlendOp::Add;
			blend.srcBlendAlpha = nvrhi::BlendFactor::One;
			blend.destBlendAlpha = nvrhi::BlendFactor::InvSrcAlpha;
			blend.blendOpAlpha = nvrhi::BlendOp::Add;
			return blend;
		}

		DecalPass::DecalGpu make_record(const RenderDecal& decal)
		{
			const glm::mat4 model = decal.World * glm::scale(glm::mat4(1.0f), decal.Size);
			const glm::mat4 inverse = glm::inverse(model);

			DecalPass::DecalGpu record;
			for (int column = 0; column < 4; column++)
			{
				record.Model[column] = model[column];
				record.Inverse[column] = inverse[column];
			}
			const glm::vec3 forward = -glm::vec3(decal.World[2]);
			const float length = glm::length(forward);
			const glm::vec3 axis = length > 1e-6f ? forward / length : glm::vec3(0.0f, 0.0f, -1.0f);
			record.Axis = glm::vec4(axis, decal.Opacity);
			// The fade runs from the angle where it starts (60 degrees by default) to the one where it is gone; as cosines, gone <= start.
			const float start = std::min(decal.AngleFadeStart, decal.AngleFadeEnd);
			const float end = std::max(decal.AngleFadeStart, decal.AngleFadeEnd);
			record.Fade = glm::vec4(decal.EdgeFade, std::cos(glm::radians(std::clamp(end, 0.0f, 90.0f))), std::cos(glm::radians(std::clamp(start, 0.0f, 90.0f))), 0.0f);
			record.Info = glm::uvec4(decal.Channels, 0u, 0u, 0u);
			return record;
		}
	}

	bool DecalPass::initialize(const PassInitializationDescription& description)
	{
		m_Device = description.Device;
		m_Shaders = description.Shaders;
		if (m_Shaders == nullptr)
		{
			WI_DECAL_LOG(Error, "The decal pass needs the shader library");
			return false;
		}

		m_Shaders->request(engine_assets::DECAL_SHADER, ShaderStage::Vertex);
		m_Shaders->request(engine_assets::DECAL_SHADER, ShaderStage::Pixel);

		// Slots match decal.hlsl (set 2 on Vulkan; registers are unique across the layouts of the pass): SRV 32 the depth, 33 its stencil,
		// 34 the prepass normals, 35 the decal records.
		nvrhi::VulkanBindingOffsets offsets;
		offsets.shaderResource = 0;
		offsets.sampler = 0;
		offsets.constantBuffer = 0;
		offsets.unorderedAccess = 0;
		nvrhi::BindingLayoutDesc layout;
		layout.visibility = nvrhi::ShaderType::All;
		layout.bindingOffsets = offsets;
		layout.addItem(nvrhi::BindingLayoutItem::Texture_SRV(32));
		layout.addItem(nvrhi::BindingLayoutItem::Texture_SRV(33));
		layout.addItem(nvrhi::BindingLayoutItem::Texture_SRV(34));
		layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(35));
		m_Layout = m_Device->createBindingLayout(layout);
		return m_Layout != nullptr;
	}

	bool DecalPass::is_ready() const
	{
		return m_Shaders->get(engine_assets::DECAL_SHADER, ShaderStage::Vertex) != nullptr && m_Shaders->get(engine_assets::DECAL_SHADER, ShaderStage::Pixel) != nullptr;
	}

	void DecalPass::update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer)
	{
		if (m_Shaders->has_failed(engine_assets::DECAL_SHADER, ShaderStage::Vertex) || m_Shaders->has_failed(engine_assets::DECAL_SHADER, ShaderStage::Pixel))
		{
			m_Failed = true;
		}
	}

	bool DecalPass::create_view_cache(ViewportTarget& viewport, ViewCache& cache)
	{
		nvrhi::BufferDesc records;
		records.byteSize = static_cast<u64>(SceneRenderData::MAX_VISIBLE_DECALS) * sizeof(DecalGpu);
		records.structStride = sizeof(DecalGpu);
		records.initialState = nvrhi::ResourceStates::ShaderResource;
		records.keepInitialState = true;
		records.debugName = "Decal records";
		cache.Records = m_Device->createBuffer(records);

		nvrhi::BindingSetDesc set;
		set.addItem(nvrhi::BindingSetItem::Texture_SRV(32, viewport.depth()));
		set.addItem(nvrhi::BindingSetItem::Texture_SRV(33, viewport.depth(), ViewportTarget::STENCIL_VIEW_FORMAT));
		set.addItem(nvrhi::BindingSetItem::Texture_SRV(34, viewport.normal()));
		set.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(35, cache.Records));
		cache.BindingSet = cache.Records ? m_Device->createBindingSet(set, m_Layout) : nullptr;
		return cache.Records && cache.BindingSet;
	}

	void DecalPass::render(nvrhi::ICommandList* commandList, const FrameDescription& frame)
	{
		if (frame.Scene == nullptr || !frame.Scene->active() || m_Failed)
		{
			return;
		}

		const SceneRenderData& data = *frame.Scene;
		ViewportTarget& viewport = *data.viewport();
		if (!data.has_camera() || !data.decals_active() || data.visible_decals().empty() || !viewport.has_decal_targets() || data.scene() == nullptr)
		{
			return;
		}

		const u32 shaderGeneration = m_Shaders->get_generation();
		if (m_Pipeline == nullptr || shaderGeneration != m_BuiltShaderGeneration)
		{
			m_BuiltShaderGeneration = shaderGeneration;

			nvrhi::GraphicsPipelineDesc pipeline;
			pipeline.primType = nvrhi::PrimitiveType::TriangleList;
			pipeline.VS = m_Shaders->get(engine_assets::DECAL_SHADER, ShaderStage::Vertex);
			pipeline.PS = m_Shaders->get(engine_assets::DECAL_SHADER, ShaderStage::Pixel);
			pipeline.addBindingLayout(data.frame_layout());
			pipeline.addBindingLayout(data.cache()->material_layout());
			pipeline.addBindingLayout(m_Layout);

			nvrhi::RenderState& state = pipeline.renderState;
			state.depthStencilState.depthTestEnable = false; // the box is tested against the depth in the shader; the camera may be inside it
			state.depthStencilState.depthWriteEnable = false;
			state.depthStencilState.stencilEnable = false;
			state.rasterState.cullMode = nvrhi::RasterCullMode::Front; // the box's back faces: they are there when the camera is inside it too
			state.rasterState.frontCounterClockwise = true;
			for (u32 target = 0; target < 3; target++)
			{
				state.blendState.targets[target] = premultiplied();
			}
			nvrhi::BlendState::RenderTarget additive;
			additive.blendEnable = true;
			additive.srcBlend = nvrhi::BlendFactor::One;
			additive.destBlend = nvrhi::BlendFactor::One;
			additive.blendOp = nvrhi::BlendOp::Add;
			additive.srcBlendAlpha = nvrhi::BlendFactor::Zero;
			additive.destBlendAlpha = nvrhi::BlendFactor::One;
			additive.blendOpAlpha = nvrhi::BlendOp::Add;
			state.blendState.targets[3] = additive;

			m_Pipeline = m_Device->createGraphicsPipeline(pipeline, viewport.decal_framebuffer());
			if (m_Pipeline == nullptr)
			{
				WI_DECAL_LOG(Error, "Could not create the decal pipeline");
				m_Failed = true;
				return;
			}
		}

		ViewCache& cache = m_PerView.get(frame.View->Target);
		if (cache.BindingSet == nullptr && !create_view_cache(viewport, cache))
		{
			WI_DECAL_LOG(Error, "Could not create the decal bindings");
			m_Failed = true;
			return;
		}

		const std::vector<RenderDecal>& decals = data.scene()->decals();
		std::vector<DecalGpu> records;
		records.reserve(data.visible_decals().size());
		for (const SceneRenderData::VisibleDecal& visible : data.visible_decals())
		{
			records.push_back(make_record(decals[visible.Slot]));
		}
		commandList->writeBuffer(cache.Records, records.data(), records.size() * sizeof(DecalGpu));

		const nvrhi::Color nothing(0.0f, 0.0f, 0.0f, 0.0f);
		commandList->clearTextureFloat(viewport.decal_albedo(), nvrhi::AllSubresources, nothing);
		commandList->clearTextureFloat(viewport.decal_normal(), nvrhi::AllSubresources, nothing);
		commandList->clearTextureFloat(viewport.decal_orm(), nvrhi::AllSubresources, nothing);
		commandList->clearTextureFloat(viewport.decal_emissive(), nvrhi::AllSubresources, nothing);

		GpuResourceCache& resources = *data.cache();
		const nvrhi::Viewport area(static_cast<float>(viewport.width()), static_cast<float>(viewport.height()));
		u32 index = 0;
		for (const SceneRenderData::VisibleDecal& visible : data.visible_decals())
		{
			nvrhi::GraphicsState state;
			state.pipeline = m_Pipeline;
			state.framebuffer = viewport.decal_framebuffer();
			state.viewport.addViewportAndScissorRect(area);
			state.addBindingSet(data.frame_bindings());
			state.addBindingSet(resources.get_material_bindings(visible.Material));
			state.addBindingSet(cache.BindingSet);
			commandList->setGraphicsState(state);

			const DrawConstants constants{ index, visible.Material, 0u };
			commandList->setPushConstants(&constants, sizeof(constants));

			nvrhi::DrawArguments arguments;
			arguments.vertexCount = CUBE_VERTICES;
			arguments.instanceCount = 1;
			commandList->draw(arguments);
			index++;
		}
	}
}
