#include <pch.h>
#include "forward_pass.h"

#include "../engine_assets.h"

#define WI_FORWARD_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	bool ForwardPass::initialize(const PassInitializationDescription& description)
	{
		m_Device = description.Device;
		m_Shaders = description.Shaders;
		if (m_Shaders == nullptr)
		{
			WI_FORWARD_LOG(Error, "The forward pass needs the shader library");
			return false;
		}

		m_Shaders->request(engine_assets::FORWARD_SHADER, ShaderStage::Vertex);
		m_Shaders->request(engine_assets::FORWARD_SHADER, ShaderStage::Pixel);
		return true;
	}

	bool ForwardPass::is_ready() const
	{
		return m_Shaders->get(engine_assets::FORWARD_SHADER, ShaderStage::Vertex) != nullptr && m_Shaders->get(engine_assets::FORWARD_SHADER, ShaderStage::Pixel) != nullptr;
	}

	void ForwardPass::update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer)
	{
		if (m_Shaders->has_failed(engine_assets::FORWARD_SHADER, ShaderStage::Vertex) || m_Shaders->has_failed(engine_assets::FORWARD_SHADER, ShaderStage::Pixel))
		{
			m_Failed = true;
		}
	}

	void ForwardPass::build_pipelines(const FrameDescription& frame)
	{
		const SceneRenderData& data = *frame.Scene;
		const u32 shaderGeneration = m_Shaders->get_generation();
		if (shaderGeneration == m_BuiltShaderGeneration && data.viewport()->generation() == m_BuiltViewportGeneration)
		{
			return;
		}
		m_BuiltShaderGeneration = shaderGeneration;
		m_BuiltViewportGeneration = data.viewport()->generation();

		nvrhi::IShader* vertexShader = m_Shaders->get(engine_assets::FORWARD_SHADER, ShaderStage::Vertex);
		nvrhi::IShader* pixelShader = m_Shaders->get(engine_assets::FORWARD_SHADER, ShaderStage::Pixel);
		m_InputLayout = scene_pass::make_mesh_input_layout(m_Device, vertexShader);

		const std::vector<nvrhi::IBindingLayout*> layouts = { data.frame_layout(), data.cache()->material_layout() };
		for (int doubleSided = 0; doubleSided < 2; doubleSided++)
		{
			scene_pass::PipelineOptions options;
			options.DoubleSided = doubleSided != 0;
			options.DepthTest = true;
			options.DepthWrite = false; // the prepass wrote the depth
			m_Pipelines[doubleSided] = scene_pass::make_pipeline(m_Device, m_InputLayout, vertexShader, pixelShader, layouts, data.viewport()->hdr_framebuffer(), options);
			if (m_Pipelines[doubleSided] == nullptr)
			{
				WI_FORWARD_LOG(Error, "Could not create the forward pipelines");
				m_Failed = true;
			}
		}
	}

	void ForwardPass::render(nvrhi::ICommandList* commandList, const FrameDescription& frame)
	{
		if (frame.Scene == nullptr || !frame.Scene->active() || m_Failed)
		{
			return;
		}

		const SceneRenderData& data = *frame.Scene;
		const glm::vec3& background = data.settings().Background;
		commandList->clearTextureFloat(data.viewport()->hdr(), nvrhi::AllSubresources, nvrhi::Color(background.r, background.g, background.b, 1.0f));

		if (!data.has_camera() || (data.opaque().empty() && data.masked().empty()))
		{
			return;
		}

		build_pipelines(frame);
		if (m_Failed)
		{
			return;
		}

		nvrhi::IFramebuffer* framebuffer = data.viewport()->hdr_framebuffer();
		const nvrhi::Viewport viewport(static_cast<float>(data.viewport()->width()), static_cast<float>(data.viewport()->height()));
		scene_pass::draw_items(commandList, data, framebuffer, viewport, data.frame_bindings(), data.opaque(), m_Pipelines[0], m_Pipelines[1], true);
		scene_pass::draw_items(commandList, data, framebuffer, viewport, data.frame_bindings(), data.masked(), m_Pipelines[0], m_Pipelines[1], true);
	}
}
