#pragma once

#include <vector>

#include "../render_pass.h"
#include "../shader_library.h"
#include "scene_pass_utils.h"

namespace wiwa
{
	// Outlines the selected entity (RenderScene::selection()) on the finished LDR image, after FXAA. Two stencil passes mark the entity's
	// draw items in the viewport depth's stencil: bit 0 everywhere they are (the silhouette), bit 1 where they pass the depth test (the
	// visible part). A fullscreen pass then draws the ring around the silhouette from the stencil, dimmer where the selection is hidden.
	// Does nothing when nothing is selected, the selection is not a drawn instance, or the outline is disabled.
	class SelectionOutlinePass : public IRenderPass
	{
	public:
		virtual const char* get_name() const override { return "Selection outline"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override;
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;
		virtual bool runs_in_view(const ViewFlags& flags, bool isMain) const override { return isMain; }

	private:
		// Mirrors OutlineData in selection_outline.hlsl.
		struct PushConstants
		{
			glm::vec3 Color{ 1.0f };
			float Width = 2.0f;
			float OccludedAlpha = 0.35f;
			float Padding[3]{};
		};

		bool build_pipelines(const SceneRenderData& data);

		nvrhi::IDevice* m_Device = nullptr;
		ShaderLibrary* m_Shaders = nullptr;
		bool m_Failed = false;

		nvrhi::InputLayoutHandle m_InputLayout{};
		nvrhi::GraphicsPipelineHandle m_VisiblePipelines[2]{};    // [double sided]: stencil 3 where the depth test passes
		nvrhi::GraphicsPipelineHandle m_SilhouettePipelines[2]{}; // stencil bit 0 everywhere
		nvrhi::BindingLayoutHandle m_OutlineLayout{};
		nvrhi::BindingSetHandle m_OutlineSet{};
		nvrhi::GraphicsPipelineHandle m_OutlinePipeline{};
		std::vector<DrawItem> m_Items[3]{}; // the selection's opaque, masked and blended draw items
		u32 m_BuiltShaderGeneration = ~0u;
		u32 m_BuiltViewportGeneration = ~0u;
	};
}
