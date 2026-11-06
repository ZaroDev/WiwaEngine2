#include <pch.h>
#include "velocity_pass.h"

#include "../engine_assets.h"

#define WI_VELOCITY_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	bool VelocityPass::initialize(const PassInitializationDescription& description)
	{
		m_Device = description.Device;
		m_Shaders = description.Shaders;
		if (m_Shaders == nullptr)
		{
			WI_VELOCITY_LOG(Error, "The velocity pass needs the shader library");
			return false;
		}

		m_Shaders->request(engine_assets::VELOCITY_BLEND_SHADER, ShaderStage::Vertex);
		m_Shaders->request(engine_assets::VELOCITY_BLEND_SHADER, ShaderStage::Pixel);
		return true;
	}

	bool VelocityPass::is_ready() const
	{
		return m_Shaders->get(engine_assets::VELOCITY_BLEND_SHADER, ShaderStage::Vertex) != nullptr
			&& m_Shaders->get(engine_assets::VELOCITY_BLEND_SHADER, ShaderStage::Pixel) != nullptr;
	}

	void VelocityPass::update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer)
	{
		if (m_Shaders->has_failed(engine_assets::VELOCITY_BLEND_SHADER, ShaderStage::Vertex) || m_Shaders->has_failed(engine_assets::VELOCITY_BLEND_SHADER, ShaderStage::Pixel))
		{
			m_Failed = true;
		}
	}

	// The pipelines depend on the shaders and on the formats of the viewport's framebuffer; they are made again when either changes.
	void VelocityPass::build_pipelines(const FrameDescription& frame)
	{
		const SceneRenderData& data = *frame.Scene;
		const u32 shaderGeneration = m_Shaders->get_generation();
		if (shaderGeneration == m_BuiltShaderGeneration && data.viewport()->generation() == m_BuiltViewportGeneration)
		{
			return;
		}
		m_BuiltShaderGeneration = shaderGeneration;
		m_BuiltViewportGeneration = data.viewport()->generation();

		nvrhi::IShader* vertexShader = m_Shaders->get(engine_assets::VELOCITY_BLEND_SHADER, ShaderStage::Vertex);
		nvrhi::IShader* pixelShader = m_Shaders->get(engine_assets::VELOCITY_BLEND_SHADER, ShaderStage::Pixel);
		m_InputLayout = scene_pass::make_mesh_input_layout(m_Device, vertexShader);

		const std::vector<nvrhi::IBindingLayout*> layouts = { data.frame_layout(), data.cache()->material_layout(), data.skin_layout() };
		for (int doubleSided = 0; doubleSided < 2; doubleSided++)
		{
			scene_pass::PipelineOptions options;
			options.DoubleSided = doubleSided != 0;
			options.DepthTest = true;
			options.DepthWrite = false; // the opaque depth stays as it is
			m_Pipelines[doubleSided] = scene_pass::make_pipeline(m_Device, m_InputLayout, vertexShader, pixelShader, layouts, data.viewport()->velocity_framebuffer(), options);
			if (m_Pipelines[doubleSided] == nullptr)
			{
				WI_VELOCITY_LOG(Error, "Could not create the transparent velocity pipelines");
				m_Failed = true;
			}
		}
	}

	void VelocityPass::render(nvrhi::ICommandList* commandList, const FrameDescription& frame)
	{
		if (frame.Scene == nullptr || !frame.Scene->active() || m_Failed)
		{
			return;
		}

		const SceneRenderData& data = *frame.Scene;
		if (!data.has_camera() || data.blended().empty())
		{
			return;
		}

		build_pipelines(frame);
		if (m_Failed)
		{
			return;
		}

		nvrhi::IFramebuffer* framebuffer = data.viewport()->velocity_framebuffer();
		const nvrhi::Viewport viewport(static_cast<float>(data.viewport()->width()), static_cast<float>(data.viewport()->height()));
		scene_pass::draw_items(commandList, data, framebuffer, viewport, data.frame_bindings(), data.blended(), m_Pipelines[0], m_Pipelines[1], true);
	}
}
