#include <pch.h>
#include "selection_outline_pass.h"

#include <algorithm>

#include "../engine_assets.h"
#include "../scene/scene_render_data.h"

#define WI_OUTLINE_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		constexpr float MIN_WIDTH = 1.0f;
		constexpr float MAX_WIDTH = 8.0f;
	}

	bool SelectionOutlinePass::initialize(const PassInitializationDescription& description)
	{
		m_Device = description.Device;
		m_Shaders = description.Shaders;
		if (m_Shaders == nullptr)
		{
			WI_OUTLINE_LOG(Error, "The selection outline pass needs the shader library");
			return false;
		}

		m_Shaders->request(engine_assets::DEPTH_SHADER, ShaderStage::Vertex); // the mask is drawn with the prepass' vertex shader: same depths
		m_Shaders->request(engine_assets::SELECTION_OUTLINE_SHADER, ShaderStage::Vertex);
		m_Shaders->request(engine_assets::SELECTION_OUTLINE_SHADER, ShaderStage::Pixel);

		// Slots match engine/shaders/selection_outline.hlsl: SRV 0, push constants 1.
		nvrhi::VulkanBindingOffsets offsets;
		offsets.shaderResource = 0;
		offsets.sampler = 0;
		offsets.constantBuffer = 0;
		offsets.unorderedAccess = 0;
		nvrhi::BindingLayoutDesc layout;
		layout.visibility = nvrhi::ShaderType::All;
		layout.bindingOffsets = offsets;
		layout.addItem(nvrhi::BindingLayoutItem::Texture_SRV(0));
		layout.addItem(nvrhi::BindingLayoutItem::PushConstants(1, sizeof(PushConstants)));
		m_OutlineLayout = m_Device->createBindingLayout(layout);
		return m_OutlineLayout != nullptr;
	}

	bool SelectionOutlinePass::is_ready() const
	{
		return m_Shaders->get(engine_assets::DEPTH_SHADER, ShaderStage::Vertex) != nullptr
			&& m_Shaders->get(engine_assets::SELECTION_OUTLINE_SHADER, ShaderStage::Vertex) != nullptr
			&& m_Shaders->get(engine_assets::SELECTION_OUTLINE_SHADER, ShaderStage::Pixel) != nullptr;
	}

	void SelectionOutlinePass::update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer)
	{
		if (m_Shaders->has_failed(engine_assets::DEPTH_SHADER, ShaderStage::Vertex) || m_Shaders->has_failed(engine_assets::SELECTION_OUTLINE_SHADER, ShaderStage::Vertex)
			|| m_Shaders->has_failed(engine_assets::SELECTION_OUTLINE_SHADER, ShaderStage::Pixel))
		{
			m_Failed = true;
		}
	}

	bool SelectionOutlinePass::build_pipelines(const SceneRenderData& data)
	{
		ViewportTarget& viewport = *data.viewport();
		nvrhi::IShader* maskVertex = m_Shaders->get(engine_assets::DEPTH_SHADER, ShaderStage::Vertex);
		m_InputLayout = scene_pass::make_mesh_input_layout(m_Device, maskVertex);
		const std::vector<nvrhi::IBindingLayout*> layouts = { data.frame_layout() };

		for (int doubleSided = 0; doubleSided < 2; doubleSided++)
		{
			scene_pass::PipelineOptions visible;
			visible.DoubleSided = doubleSided != 0;
			visible.DepthTest = true;
			visible.DepthWrite = false;
			visible.StencilWrite = true;
			visible.StencilRef = 3;
			visible.StencilMask = 3;
			visible.NoColorWrite = true;
			m_VisiblePipelines[doubleSided] = scene_pass::make_pipeline(m_Device, m_InputLayout, maskVertex, nullptr, layouts, viewport.ldr_depth_framebuffer(), visible);

			scene_pass::PipelineOptions silhouette = visible;
			silhouette.DepthTest = false;
			silhouette.StencilRef = 1;
			silhouette.StencilMask = 1;
			m_SilhouettePipelines[doubleSided] = scene_pass::make_pipeline(m_Device, m_InputLayout, maskVertex, nullptr, layouts, viewport.ldr_depth_framebuffer(), silhouette);
			if (m_VisiblePipelines[doubleSided] == nullptr || m_SilhouettePipelines[doubleSided] == nullptr)
			{
				return false;
			}
		}

		nvrhi::GraphicsPipelineDesc outline;
		outline.primType = nvrhi::PrimitiveType::TriangleList;
		outline.VS = m_Shaders->get(engine_assets::SELECTION_OUTLINE_SHADER, ShaderStage::Vertex);
		outline.PS = m_Shaders->get(engine_assets::SELECTION_OUTLINE_SHADER, ShaderStage::Pixel);
		outline.addBindingLayout(m_OutlineLayout);
		outline.renderState.depthStencilState.depthTestEnable = false;
		outline.renderState.depthStencilState.depthWriteEnable = false;
		outline.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
		nvrhi::BlendState::RenderTarget& blend = outline.renderState.blendState.targets[0];
		blend.blendEnable = true;
		blend.srcBlend = nvrhi::BlendFactor::SrcAlpha;
		blend.destBlend = nvrhi::BlendFactor::InvSrcAlpha;
		blend.blendOp = nvrhi::BlendOp::Add;
		blend.srcBlendAlpha = nvrhi::BlendFactor::One;
		blend.destBlendAlpha = nvrhi::BlendFactor::InvSrcAlpha;
		blend.blendOpAlpha = nvrhi::BlendOp::Add;
		m_OutlinePipeline = m_Device->createGraphicsPipeline(outline, viewport.ldr_framebuffer());

		nvrhi::BindingSetDesc set;
		set.addItem(nvrhi::BindingSetItem::Texture_SRV(0, viewport.depth(), ViewportTarget::STENCIL_VIEW_FORMAT));
		set.addItem(nvrhi::BindingSetItem::PushConstants(1, sizeof(PushConstants)));
		m_OutlineSet = m_Device->createBindingSet(set, m_OutlineLayout);
		return m_OutlinePipeline != nullptr && m_OutlineSet != nullptr;
	}

	void SelectionOutlinePass::render(nvrhi::ICommandList* commandList, const FrameDescription& frame)
	{
		if (m_Failed || frame.Scene == nullptr || !frame.Scene->active() || !frame.Scene->has_camera() || frame.Scene->scene() == nullptr)
		{
			return;
		}

		const SceneRenderData& data = *frame.Scene;
		const RenderScene& scene = *data.scene();
		const SelectionOutlineStyle& style = scene.outline_style();
		if (!style.Enabled || !scene.selection())
		{
			return;
		}
		const std::optional<size_t> slot = scene.find_instance(*scene.selection());
		if (!slot)
		{
			return;
		}

		// The selection's share of what the camera sees.
		const std::vector<DrawItem>* lists[3] = { &data.opaque(), &data.masked(), &data.blended() };
		size_t total = 0;
		for (int i = 0; i < 3; i++)
		{
			m_Items[i].clear();
			std::copy_if(lists[i]->begin(), lists[i]->end(), std::back_inserter(m_Items[i]), [&](const DrawItem& item) { return item.Instance == *slot; });
			total += m_Items[i].size();
		}
		if (total == 0)
		{
			return;
		}

		ViewportTarget& viewport = *data.viewport();
		const u32 shaderGeneration = m_Shaders->get_generation();
		if (shaderGeneration != m_BuiltShaderGeneration || viewport.generation() != m_BuiltViewportGeneration)
		{
			m_BuiltShaderGeneration = shaderGeneration;
			m_BuiltViewportGeneration = viewport.generation();
			if (!build_pipelines(data))
			{
				WI_OUTLINE_LOG(Error, "Could not create the selection outline pipelines");
				m_Failed = true;
				return;
			}
		}

		const nvrhi::Viewport area(static_cast<float>(viewport.width()), static_cast<float>(viewport.height()));
		nvrhi::IFramebuffer* framebuffer = viewport.ldr_depth_framebuffer();
		for (int pass = 0; pass < 2; pass++)
		{
			nvrhi::GraphicsPipelineHandle* pipelines = pass == 0 ? m_VisiblePipelines : m_SilhouettePipelines;
			for (int i = 0; i < 3; i++)
			{
				scene_pass::draw_items(commandList, data, framebuffer, area, data.frame_bindings(), m_Items[i], pipelines[0], pipelines[1], false);
			}
		}

		PushConstants constants;
		constants.Color = style.Color;
		constants.Width = std::clamp(style.WidthPx, MIN_WIDTH, MAX_WIDTH);
		constants.OccludedAlpha = std::clamp(style.OccludedAlpha, 0.0f, 1.0f);

		nvrhi::GraphicsState state;
		state.pipeline = m_OutlinePipeline;
		state.framebuffer = viewport.ldr_framebuffer();
		state.viewport.addViewportAndScissorRect(area);
		state.addBindingSet(m_OutlineSet);
		commandList->setGraphicsState(state);
		commandList->setPushConstants(&constants, sizeof(constants));
		nvrhi::DrawArguments arguments;
		arguments.vertexCount = 3;
		commandList->draw(arguments);
	}
}
