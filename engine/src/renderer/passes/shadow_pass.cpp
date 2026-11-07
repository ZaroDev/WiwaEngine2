#include <pch.h>
#include "shadow_pass.h"

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

	// Framebuffers and pipelines depend on the shadow maps (made on demand) and on the shader.
	void ShadowPass::rebuild(const SceneRenderData& data)
	{
		const u32 shaderGeneration = m_Shaders->get_generation();
		if (shaderGeneration == m_BuiltShaderGeneration && data.shadow_generation() == m_BuiltShadowGeneration)
		{
			return;
		}
		m_BuiltShaderGeneration = shaderGeneration;
		m_BuiltShadowGeneration = data.shadow_generation();

		nvrhi::IShader* vertexShader = m_Shaders->get(engine_assets::SHADOW_SHADER, ShaderStage::Vertex);
		m_InputLayout = scene_pass::make_mesh_input_layout(m_Device, vertexShader);

		scene_pass::PipelineOptions options;
		options.DoubleSided = true;
		options.DepthTest = true;
		options.DepthWrite = true;
		const std::vector<nvrhi::IBindingLayout*> layouts = { m_Layout, data.skin_layout() };
		// casters with a surface shader read the whole frame set (with stand-ins for the maps being drawn): frame, material, AO, surface, skin
		const std::vector<nvrhi::IBindingLayout*> surfaceLayouts = { data.frame_layout(), data.cache()->material_layout(), data.ao_layout(), data.cache()->custom_layout(), data.skin_layout() };

		m_CascadePipeline = nullptr;
		m_AtlasPipeline = nullptr;
		for (nvrhi::FramebufferHandle& framebuffer : m_CascadeFramebuffers)
		{
			framebuffer = nullptr;
		}
		m_AtlasFramebuffer = nullptr;

		if (data.cascade_map() != nullptr)
		{
			for (u32 slice = 0; slice < MAX_CASCADES; slice++)
			{
				nvrhi::FramebufferDesc description;
				description.setDepthAttachment(nvrhi::FramebufferAttachment().setTexture(data.cascade_map()).setArraySlice(slice));
				m_CascadeFramebuffers[slice] = m_Device->createFramebuffer(description);
			}
			m_CascadePipeline = scene_pass::make_pipeline(m_Device, m_InputLayout, vertexShader, nullptr, layouts, m_CascadeFramebuffers[0], options);
			m_SurfaceCascade.configure(m_Device, m_CascadeFramebuffers[0], surfaceLayouts, options);
		}
		if (data.spot_atlas() != nullptr)
		{
			nvrhi::FramebufferDesc description;
			description.setDepthAttachment(data.spot_atlas());
			m_AtlasFramebuffer = m_Device->createFramebuffer(description);
			m_AtlasPipeline = scene_pass::make_pipeline(m_Device, m_InputLayout, vertexShader, nullptr, layouts, m_AtlasFramebuffer, options);
			m_SurfaceAtlas.configure(m_Device, m_AtlasFramebuffer, surfaceLayouts, options);
		}
	}

	void ShadowPass::render(nvrhi::ICommandList* commandList, const FrameDescription& frame)
	{
		if (frame.Scene == nullptr || !frame.Scene->active() || m_Failed || frame.Scene->shadow_views().empty())
		{
			return;
		}

		const SceneRenderData& data = *frame.Scene;
		rebuild(data);
		if ((data.cascade_map() && !m_CascadePipeline) || (data.spot_atlas() && !m_AtlasPipeline))
		{
			WI_SHADOW_LOG(Error, "Could not create the shadow pipelines");
			m_Failed = true;
			return;
		}

		if (m_BindingSet == nullptr || m_BoundInstances != data.instance_buffer())
		{
			nvrhi::BindingSetDesc set;
			set.addItem(nvrhi::BindingSetItem::ConstantBuffer(0, data.constant_buffer()));
			set.addItem(nvrhi::BindingSetItem::StructuredBuffer_SRV(1, data.instance_buffer()));
			set.addItem(nvrhi::BindingSetItem::PushConstants(1, 12));
			m_BindingSet = m_Device->createBindingSet(set, m_Layout);
			m_BoundInstances = data.instance_buffer();
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
				const float x = static_cast<float>((view.Slice % 4) * SPOT_TILE_SIZE);
				const float y = static_cast<float>((view.Slice / 4) * SPOT_TILE_SIZE);
				viewport = nvrhi::Viewport(x, x + SPOT_TILE_SIZE, y, y + SPOT_TILE_SIZE, 0.0f, 1.0f);
				framebuffer = m_AtlasFramebuffer;
				pipeline = m_AtlasPipeline;
				surface.Pipelines = &m_SurfaceAtlas;
			}
			else
			{
				commandList->clearDepthStencilTexture(data.cascade_map(), nvrhi::TextureSubresourceSet(0, 1, view.Slice, 1), true, 1.0f, false, 0);
				viewport = nvrhi::Viewport(static_cast<float>(CASCADE_MAP_SIZE), static_cast<float>(CASCADE_MAP_SIZE));
				framebuffer = m_CascadeFramebuffers[view.Slice];
				pipeline = m_CascadePipeline;
				surface.Pipelines = &m_SurfaceCascade;
			}

			scene_pass::draw_items(commandList, data, framebuffer, viewport, m_BindingSet, view.Items, pipeline, pipeline, false, nullptr, &surface);
		}
	}
}
