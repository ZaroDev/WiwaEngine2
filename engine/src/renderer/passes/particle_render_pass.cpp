#include <pch.h>
#include "particle_render_pass.h"

#include "../engine_assets.h"
#include "../scene/scene_render_data.h"
#include "../view/view_state.h"

#define WI_PARTICLE_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		// Mirrors DrawConstantsData of scene_common.hlsli: InstanceIndex is the emitter's id, MaterialSlot its material.
		struct DrawConstants
		{
			u32 InstanceIndex;
			u32 MaterialSlot;
			u32 BaseVertex;
		};

		constexpr u32 SPRITE_VERTICES = 6;

		nvrhi::BlendState::RenderTarget blend_for(ParticleBlend blend)
		{
			nvrhi::BlendState::RenderTarget target;
			target.blendEnable = true;
			target.blendOp = nvrhi::BlendOp::Add;
			target.blendOpAlpha = nvrhi::BlendOp::Add;
			switch (blend)
			{
			case ParticleBlend::Additive:
				target.srcBlend = nvrhi::BlendFactor::One;
				target.destBlend = nvrhi::BlendFactor::One;
				target.srcBlendAlpha = nvrhi::BlendFactor::One; // a glow adds to the coverage too: on a transparent background (a thumbnail) it is seen
				target.destBlendAlpha = nvrhi::BlendFactor::One;
				break;
			case ParticleBlend::Multiply:
				target.srcBlend = nvrhi::BlendFactor::DstColor;
				target.destBlend = nvrhi::BlendFactor::Zero;
				target.srcBlendAlpha = nvrhi::BlendFactor::Zero;
				target.destBlendAlpha = nvrhi::BlendFactor::One;
				break;
			case ParticleBlend::Alpha:
			default:
				target.srcBlend = nvrhi::BlendFactor::One; // premultiplied
				target.destBlend = nvrhi::BlendFactor::InvSrcAlpha;
				target.srcBlendAlpha = nvrhi::BlendFactor::One;
				target.destBlendAlpha = nvrhi::BlendFactor::InvSrcAlpha;
				break;
			}
			return target;
		}
	}

	bool ParticleRenderPass::initialize(const PassInitializationDescription& description)
	{
		m_Device = description.Device;
		m_Shaders = description.Shaders;
		m_Pool = description.Particles;
		if (m_Shaders == nullptr || m_Pool == nullptr)
		{
			WI_PARTICLE_LOG(Error, "The particle render pass needs the shader library and the pool");
			return false;
		}

		m_Shaders->request(engine_assets::PARTICLE_SPRITE_SHADER, ShaderStage::Vertex);
		m_Shaders->request(engine_assets::PARTICLE_SPRITE_SHADER, ShaderStage::Pixel);

		// Slots match particle_sprite.hlsl (set 2 on Vulkan; registers are unique across the layouts of the pass): SRV 36 the particles, 37 the emitters,
		// 38 the tables, 39 the scene depth, 40 the sort entries.
		nvrhi::VulkanBindingOffsets offsets;
		offsets.shaderResource = 0;
		offsets.sampler = 0;
		offsets.constantBuffer = 0;
		offsets.unorderedAccess = 0;
		nvrhi::BindingLayoutDesc layout;
		layout.visibility = nvrhi::ShaderType::All;
		layout.bindingOffsets = offsets;
		layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(36));
		layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(37));
		layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(38));
		layout.addItem(nvrhi::BindingLayoutItem::Texture_SRV(39));
		layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(40));
		m_Layout = m_Device->createBindingLayout(layout);
		return m_Layout != nullptr;
	}

	bool ParticleRenderPass::is_ready() const
	{
		return m_Shaders->get(engine_assets::PARTICLE_SPRITE_SHADER, ShaderStage::Vertex) != nullptr && m_Shaders->get(engine_assets::PARTICLE_SPRITE_SHADER, ShaderStage::Pixel) != nullptr;
	}

	void ParticleRenderPass::update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer)
	{
		if (m_Shaders->has_failed(engine_assets::PARTICLE_SPRITE_SHADER, ShaderStage::Vertex) || m_Shaders->has_failed(engine_assets::PARTICLE_SPRITE_SHADER, ShaderStage::Pixel))
		{
			m_Failed = true;
		}
	}

	void ParticleRenderPass::render(nvrhi::ICommandList* commandList, const FrameDescription& frame)
	{
		ParticlePool& pool = m_Pool->for_view(frame.View->Target.uid(), frame.IsMainView);
		if (frame.Scene == nullptr || !frame.Scene->active() || m_Failed || pool.Draws.empty() || !pool.ready() || !frame.Scene->has_camera())
		{
			return;
		}

		const SceneRenderData& data = *frame.Scene;
		ViewportTarget& viewport = *data.viewport();
		if (viewport.hdr_color_framebuffer() == nullptr)
		{
			return;
		}

		const u32 shaderGeneration = m_Shaders->get_generation();
		if (m_Pipelines[0] == nullptr || shaderGeneration != m_BuiltShaderGeneration)
		{
			m_BuiltShaderGeneration = shaderGeneration;
			for (u32 mode = 0; mode < 3; mode++)
			{
				nvrhi::GraphicsPipelineDesc pipeline;
				pipeline.primType = nvrhi::PrimitiveType::TriangleList;
				pipeline.VS = m_Shaders->get(engine_assets::PARTICLE_SPRITE_SHADER, ShaderStage::Vertex);
				pipeline.PS = m_Shaders->get(engine_assets::PARTICLE_SPRITE_SHADER, ShaderStage::Pixel);
				pipeline.addBindingLayout(data.frame_layout());
				pipeline.addBindingLayout(data.cache()->material_layout());
				pipeline.addBindingLayout(m_Layout);

				nvrhi::RenderState& state = pipeline.renderState;
				state.depthStencilState.depthTestEnable = false; // the shader tests against the depth it reads
				state.depthStencilState.depthWriteEnable = false;
				state.depthStencilState.stencilEnable = false;
				state.rasterState.cullMode = nvrhi::RasterCullMode::None;
				state.blendState.targets[0] = blend_for(static_cast<ParticleBlend>(mode));
				m_Pipelines[mode] = m_Device->createGraphicsPipeline(pipeline, viewport.hdr_color_framebuffer());
				if (m_Pipelines[mode] == nullptr)
				{
					WI_PARTICLE_LOG(Error, "Could not create the particle pipelines");
					m_Failed = true;
					return;
				}
			}
		}

		ViewCache& cache = m_PerView.get(frame.View->Target);
		if (cache.BindingSet == nullptr || cache.Pool != &pool || cache.PoolGeneration != pool.generation() || cache.SortGeneration != pool.sort_generation())
		{
			nvrhi::BindingSetDesc set;
			set.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(36, pool.particles()));
			set.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(37, pool.emitters()));
			set.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(38, pool.tables()));
			set.addItem(nvrhi::BindingSetItem::Texture_SRV(39, viewport.depth()));
			set.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(40, pool.sort_buffer()));
			cache.BindingSet = m_Device->createBindingSet(set, m_Layout);
			cache.Pool = &pool;
			cache.PoolGeneration = pool.generation();
			cache.SortGeneration = pool.sort_generation();
			if (cache.BindingSet == nullptr)
			{
				WI_PARTICLE_LOG(Error, "Could not create the particle bindings");
				m_Failed = true;
				return;
			}
		}

		GpuResourceCache& resources = *data.cache();
		const nvrhi::Viewport area(static_cast<float>(viewport.width()), static_cast<float>(viewport.height()));
		for (const ParticleDraw& draw : pool.Draws)
		{
			const u32 material = resources.get_material_slot(draw.Material);

			nvrhi::GraphicsState state;
			state.pipeline = m_Pipelines[static_cast<u32>(draw.Blend)];
			state.framebuffer = viewport.hdr_color_framebuffer();
			state.viewport.addViewportAndScissorRect(area);
			state.addBindingSet(data.frame_bindings());
			state.addBindingSet(resources.get_material_bindings(material));
			state.addBindingSet(cache.BindingSet);
			commandList->setGraphicsState(state);

			const DrawConstants constants{ draw.Id, material, 0u };
			commandList->setPushConstants(&constants, sizeof(constants));

			nvrhi::DrawArguments arguments;
			arguments.vertexCount = SPRITE_VERTICES;
			arguments.instanceCount = draw.Capacity;
			commandList->draw(arguments);
		}
	}
}
