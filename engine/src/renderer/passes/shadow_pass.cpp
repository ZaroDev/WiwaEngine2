#include <pch.h>
#include "shadow_pass.h"

#include "../view/view_state.h"

#include "../engine_assets.h"
#include "../scene/cascade_math.h"
#include "../scene/scene_render_data.h"

#define WI_SHADOW_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	bool ShadowPass::initialize(const PassInitializationDescription& description)
	{
		m_Device = description.Device;
		m_Shaders = description.Shaders;
		if (m_Shaders == nullptr)
		{
			WI_SHADOW_LOG(Error, "The shadow pass needs the shader library");
			return false;
		}

		m_Shaders->request(engine_assets::SHADOW_SHADER, ShaderStage::Vertex);

		// The frame set without the shadow maps (they are being drawn into): constants, instances, push constants.
		nvrhi::VulkanBindingOffsets offsets;
		offsets.shaderResource = 0;
		offsets.sampler = 0;
		offsets.constantBuffer = 0;
		offsets.unorderedAccess = 0;
		nvrhi::BindingLayoutDesc layout;
		layout.visibility = nvrhi::ShaderType::Vertex;
		layout.bindingOffsets = offsets;
		layout.addItem(nvrhi::BindingLayoutItem::VolatileConstantBuffer(0));
		layout.addItem(nvrhi::BindingLayoutItem::StructuredBuffer_SRV(1));
		layout.addItem(nvrhi::BindingLayoutItem::PushConstants(1, 12));
		m_Layout = m_Device->createBindingLayout(layout);
		return m_Layout != nullptr;
	}

	bool ShadowPass::is_ready() const
	{
		return m_Shaders->get(engine_assets::SHADOW_SHADER, ShaderStage::Vertex) != nullptr;
	}

	void ShadowPass::update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer)
	{
		if (m_Shaders->has_failed(engine_assets::SHADOW_SHADER, ShaderStage::Vertex))
		{
			m_Failed = true;
		}
	}

	// The pipelines depend on the shader only (every view has depth maps of the same formats, and the layouts are shared); the framebuffers
	// depend on the view's shadow maps (made on demand).
	void ShadowPass::rebuild(const SceneRenderData& data, ViewCache& cache)
	{
		const u32 shaderGeneration = m_Shaders->get_generation();
		const bool shaderChanged = shaderGeneration != m_BuiltShaderGeneration;
		if (!shaderChanged && data.shadow_generation() == cache.ShadowGeneration && (m_CascadePipeline != nullptr || data.cascade_map() == nullptr) && (m_AtlasPipeline != nullptr || data.spot_atlas() == nullptr))
		{
			return;
		}
		m_BuiltShaderGeneration = shaderGeneration;
		cache.ShadowGeneration = data.shadow_generation();

		nvrhi::IShader* vertexShader = m_Shaders->get(engine_assets::SHADOW_SHADER, ShaderStage::Vertex);
		if (shaderChanged)
		{
			m_InputLayout = scene_pass::make_mesh_input_layout(m_Device, vertexShader);
			m_CascadePipeline = nullptr;
			m_AtlasPipeline = nullptr;
		}

		scene_pass::PipelineOptions options;
		options.DoubleSided = true;
		options.DepthTest = true;
		options.DepthWrite = true;
		const std::vector<nvrhi::IBindingLayout*> layouts = { m_Layout, data.skin_layout() };
		// casters with a surface shader read the whole frame set (with stand-ins for the maps being drawn): frame, material, AO, surface, skin
		const std::vector<nvrhi::IBindingLayout*> surfaceLayouts = { data.frame_layout(), data.cache()->material_layout(), data.ao_layout(), data.cache()->custom_layout(), data.skin_layout() };

		for (nvrhi::FramebufferHandle& framebuffer : cache.CascadeFramebuffers)
		{
			framebuffer = nullptr;
		}
		cache.AtlasFramebuffer = nullptr;

		if (data.cascade_map() != nullptr)
		{
			for (u32 slice = 0; slice < MAX_CASCADES; slice++)
			{
				nvrhi::FramebufferDesc description;
				description.setDepthAttachment(nvrhi::FramebufferAttachment().setTexture(data.cascade_map()).setArraySlice(slice));
				cache.CascadeFramebuffers[slice] = m_Device->createFramebuffer(description);
			}
			if (m_CascadePipeline == nullptr)
			{
				m_CascadePipeline = scene_pass::make_pipeline(m_Device, m_InputLayout, vertexShader, nullptr, layouts, cache.CascadeFramebuffers[0], options);
				m_SurfaceCascade.configure(m_Device, surfaceLayouts, options);
			}
		}
		if (data.spot_atlas() != nullptr)
		{
			nvrhi::FramebufferDesc description;
			description.setDepthAttachment(data.spot_atlas());
			cache.AtlasFramebuffer = m_Device->createFramebuffer(description);
			if (m_AtlasPipeline == nullptr)
			{
				m_AtlasPipeline = scene_pass::make_pipeline(m_Device, m_InputLayout, vertexShader, nullptr, layouts, cache.AtlasFramebuffer, options);
				m_SurfaceAtlas.configure(m_Device, surfaceLayouts, options);
			}
		}
	}

	void ShadowPass::render(nvrhi::ICommandList* commandList, const FrameDescription& frame)
	{
		if (frame.Scene == nullptr || !frame.Scene->active() || m_Failed || frame.Scene->shadow_views().empty())
		{
			return;
		}

		const SceneRenderData& data = *frame.Scene;
		ViewCache& cache = m_PerView.get(frame.View->Target);
		rebuild(data, cache);
		if ((data.cascade_map() && !m_CascadePipeline) || (data.spot_atlas() && !m_AtlasPipeline))
		{
			WI_SHADOW_LOG(Error, "Could not create the shadow pipelines");
			m_Failed = true;
			return;
		}

		if (cache.BindingSet == nullptr || cache.BoundInstances != data.instance_buffer())
		{
			nvrhi::BindingSetDesc set;
			set.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, data.constant_buffer()));
			set.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(1, data.instance_buffer()));
			set.addItem(nvrhi::BindingSetItem::PushConstants(1, 12));
			cache.BindingSet = m_Device->createBindingSet(set, m_Layout);
			cache.BoundInstances = data.instance_buffer();
		}

		bool atlasCleared = false;
		for (const ShadowView& view : data.shadow_views())
		{
			nvrhi::IFramebuffer* framebuffer = nullptr;
			nvrhi::IGraphicsPipeline* pipeline = nullptr;
			scene_pass::SurfaceDraw surface;
			surface.Kind = SurfaceVariantKind::Shadow;
			surface.MaskedKind = SurfaceVariantKind::ShadowMasked;
			surface.FrameBindings = data.shadow_frame_bindings();
			surface.AoBindings = data.ao_bindings();
			surface.Caster = true;
			nvrhi::Viewport viewport;
			if (view.Atlas)
			{
				if (!atlasCleared)
				{
					commandList->clearDepthStencilTexture(data.spot_atlas(), nvrhi::AllSubresources, true, 1.0f, false, 0);
					atlasCleared = true;
				}
				const float x = static_cast<float>((view.Slice % SPOT_GRID) * SPOT_TILE_SIZE);
				const float y = static_cast<float>((view.Slice / SPOT_GRID) * SPOT_TILE_SIZE);
				viewport = nvrhi::Viewport(x, x + SPOT_TILE_SIZE, y, y + SPOT_TILE_SIZE, 0.0f, 1.0f);
				framebuffer = cache.AtlasFramebuffer;
				pipeline = m_AtlasPipeline;
				surface.Pipelines = &m_SurfaceAtlas;
			}
			else
			{
				commandList->clearDepthStencilTexture(data.cascade_map(), nvrhi::TextureSubresourceSet(0, 1, view.Slice, 1), true, 1.0f, false, 0);
				viewport = nvrhi::Viewport(static_cast<float>(CASCADE_MAP_SIZE), static_cast<float>(CASCADE_MAP_SIZE));
				framebuffer = cache.CascadeFramebuffers[view.Slice];
				pipeline = m_CascadePipeline;
				surface.Pipelines = &m_SurfaceCascade;
			}

			scene_pass::draw_items(commandList, data, framebuffer, viewport, cache.BindingSet, view.Items, pipeline, pipeline, false, nullptr, &surface);
		}
	}
}
