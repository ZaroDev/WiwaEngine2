#include <pch.h>
#include "depth_prepass.h"

#include "../engine_assets.h"

#define WI_DEPTH_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	bool DepthPrepass::initialize(const PassInitializationDescription& description)
	{
		m_Device = description.Device;
		m_Shaders = description.Shaders;
		if (m_Shaders == nullptr)
		{
			WI_DEPTH_LOG(Error, "The depth prepass needs the shader library");
			return false;
		}

		m_Shaders->request(engine_assets::DEPTH_SHADER, ShaderStage::Vertex);
		m_Shaders->request(engine_assets::DEPTH_SHADER, ShaderStage::Pixel);
		m_Shaders->request(engine_assets::DEPTH_MASK_SHADER, ShaderStage::Vertex);
		m_Shaders->request(engine_assets::DEPTH_MASK_SHADER, ShaderStage::Pixel);
		return true;
	}

	bool DepthPrepass::is_ready() const
	{
		return m_Shaders->get(engine_assets::DEPTH_SHADER, ShaderStage::Vertex) != nullptr
			&& m_Shaders->get(engine_assets::DEPTH_SHADER, ShaderStage::Pixel) != nullptr
			&& m_Shaders->get(engine_assets::DEPTH_MASK_SHADER, ShaderStage::Vertex) != nullptr
			&& m_Shaders->get(engine_assets::DEPTH_MASK_SHADER, ShaderStage::Pixel) != nullptr;
	}

	void DepthPrepass::update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer)
	{
		if (m_Shaders->has_failed(engine_assets::DEPTH_SHADER, ShaderStage::Vertex) || m_Shaders->has_failed(engine_assets::DEPTH_SHADER, ShaderStage::Pixel) || m_Shaders->has_failed(engine_assets::DEPTH_MASK_SHADER, ShaderStage::Vertex)
			|| m_Shaders->has_failed(engine_assets::DEPTH_MASK_SHADER, ShaderStage::Pixel))
		{
			m_Failed = true;
		}
	}

	// The pipelines depend on the shaders and on the formats of the viewport's framebuffer; they are made again when either changes.
	void DepthPrepass::build_pipelines(const FrameDescription& frame)
	{
		const SceneRenderData& data = *frame.Scene;
		const u32 shaderGeneration = m_Shaders->get_generation();
		// Made once per shader generation: every view has the same target formats and shares the binding layouts of the first (see SceneRenderData::initialize).
		if (shaderGeneration == m_BuiltShaderGeneration)
		{
			return;
		}
		m_BuiltShaderGeneration = shaderGeneration;

		nvrhi::IShader* opaqueVertex = m_Shaders->get(engine_assets::DEPTH_SHADER, ShaderStage::Vertex);
		nvrhi::IShader* opaquePixel = m_Shaders->get(engine_assets::DEPTH_SHADER, ShaderStage::Pixel);
		nvrhi::IShader* maskedVertex = m_Shaders->get(engine_assets::DEPTH_MASK_SHADER, ShaderStage::Vertex);
		nvrhi::IShader* maskedPixel = m_Shaders->get(engine_assets::DEPTH_MASK_SHADER, ShaderStage::Pixel);
		nvrhi::IFramebuffer* framebuffer = data.viewport()->prepass_framebuffer();

		m_OpaqueInputLayout = scene_pass::make_mesh_input_layout(m_Device, opaqueVertex);
		m_MaskedInputLayout = scene_pass::make_mesh_input_layout(m_Device, maskedVertex);

		const std::vector<nvrhi::IBindingLayout*> opaqueLayouts = { data.frame_layout(), data.skin_layout() };
		const std::vector<nvrhi::IBindingLayout*> maskedLayouts = { data.frame_layout(), data.cache()->material_layout(), data.skin_layout() };

		// materials with a surface shader: frame, material, AO, the surface set and skin (surface_depth.hlsl)
		scene_pass::PipelineOptions surfaceOptions;
		surfaceOptions.DepthTest = true;
		surfaceOptions.DepthWrite = true;
		m_Surface.configure(m_Device, { data.frame_layout(), data.cache()->material_layout(), data.ao_layout(), data.cache()->custom_layout(), data.skin_layout() }, surfaceOptions);
		surfaceOptions.StencilWrite = true;
		surfaceOptions.StencilRef = DECAL_STENCIL_BIT;
		surfaceOptions.StencilMask = DECAL_STENCIL_BIT;
		m_SurfaceNoDecals.configure(m_Device, { data.frame_layout(), data.cache()->material_layout(), data.ao_layout(), data.cache()->custom_layout(), data.skin_layout() }, surfaceOptions);

		for (int doubleSided = 0; doubleSided < 2; doubleSided++)
		{
			scene_pass::PipelineOptions options;
			options.DoubleSided = doubleSided != 0;
			options.DepthTest = true;
			options.DepthWrite = true;
			m_Opaque[doubleSided] = scene_pass::make_pipeline(m_Device, m_OpaqueInputLayout, opaqueVertex, opaquePixel, opaqueLayouts, framebuffer, options);
			m_Masked[doubleSided] = scene_pass::make_pipeline(m_Device, m_MaskedInputLayout, maskedVertex, maskedPixel, maskedLayouts, framebuffer, options);
			options.StencilWrite = true;
			options.StencilRef = DECAL_STENCIL_BIT;
			options.StencilMask = DECAL_STENCIL_BIT;
			m_OpaqueNoDecals[doubleSided] = scene_pass::make_pipeline(m_Device, m_OpaqueInputLayout, opaqueVertex, opaquePixel, opaqueLayouts, framebuffer, options);
			m_MaskedNoDecals[doubleSided] = scene_pass::make_pipeline(m_Device, m_MaskedInputLayout, maskedVertex, maskedPixel, maskedLayouts, framebuffer, options);
			if (m_Opaque[doubleSided] == nullptr || m_Masked[doubleSided] == nullptr || m_OpaqueNoDecals[doubleSided] == nullptr || m_MaskedNoDecals[doubleSided] == nullptr)
			{
				WI_DEPTH_LOG(Error, "Could not create the depth prepass pipelines");
				m_Failed = true;
			}
		}
	}

	void DepthPrepass::render(nvrhi::ICommandList* commandList, const FrameDescription& frame)
	{
		if (frame.Scene == nullptr || !frame.Scene->active())
		{
			return;
		}

		// Cleared even when this pass has failed: the outline pass reads the stencil, and a frame must not inherit the last one's.
		const SceneRenderData& data = *frame.Scene;
		commandList->clearDepthStencilTexture(data.viewport()->depth(), nvrhi::AllSubresources, true, 1.0f, true, 0);
		commandList->clearTextureFloat(data.viewport()->normal(), nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 0.0f));
		commandList->clearTextureFloat(data.viewport()->velocity(), nvrhi::AllSubresources, nvrhi::Color(0.0f, 0.0f, 0.0f, 0.0f));

		if (m_Failed || !data.has_camera() || (data.opaque().empty() && data.masked().empty()))
		{
			return;
		}

		build_pipelines(frame);
		if (m_Failed)
		{
			return;
		}

		nvrhi::IFramebuffer* framebuffer = data.viewport()->prepass_framebuffer();
		const nvrhi::Viewport viewport(static_cast<float>(data.viewport()->width()), static_cast<float>(data.viewport()->height()));
		scene_pass::SurfaceDraw surface;
		surface.Pipelines = &m_Surface;
		surface.Kind = SurfaceVariantKind::Depth;
		surface.MaskedKind = SurfaceVariantKind::DepthMasked;
		surface.AoBindings = data.ao_bindings();
		scene_pass::draw_items(commandList, data, framebuffer, viewport, data.frame_bindings(), data.opaque(), m_Opaque[0], m_Opaque[1], false, nullptr, &surface, scene_pass::DecalFilter::Receivers);
		scene_pass::draw_items(commandList, data, framebuffer, viewport, data.frame_bindings(), data.masked(), m_Masked[0], m_Masked[1], true, nullptr, &surface, scene_pass::DecalFilter::Receivers);

		// Meshes that opt out of decals mark their pixels in the stencil (the decal pass skips them).
		scene_pass::SurfaceDraw surfaceNoDecals = surface;
		surfaceNoDecals.Pipelines = &m_SurfaceNoDecals;
		scene_pass::draw_items(commandList, data, framebuffer, viewport, data.frame_bindings(), data.opaque(), m_OpaqueNoDecals[0], m_OpaqueNoDecals[1], false, nullptr, &surfaceNoDecals, scene_pass::DecalFilter::NonReceivers);
		scene_pass::draw_items(commandList, data, framebuffer, viewport, data.frame_bindings(), data.masked(), m_MaskedNoDecals[0], m_MaskedNoDecals[1], true, nullptr, &surfaceNoDecals, scene_pass::DecalFilter::NonReceivers);
	}
}
