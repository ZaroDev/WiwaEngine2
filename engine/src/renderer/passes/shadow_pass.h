#pragma once

#include "../render_pass.h"
#include "../shader_library.h"
#include "scene_pass_utils.h"

namespace wiwa
{
	// Draws the shadow maps the scene data lists for this frame: the sun's cascades (slices of a depth array) and the spot lights'
	// tiles of an atlas. Depth only, no culling of back faces; the forward pass samples the maps.
	class ShadowPass : public IRenderPass
	{
	public:
		virtual const char* get_name() const override { return "Shadows"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override;
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;

	private:
		void rebuild(const SceneRenderData& data);

		nvrhi::IDevice* m_Device = nullptr;
		ShaderLibrary* m_Shaders = nullptr;
		bool m_Failed = false;

		nvrhi::BindingLayoutHandle m_Layout{};
		nvrhi::BindingSetHandle m_BindingSet{};
		const nvrhi::IBuffer* m_BoundInstances = nullptr;
		nvrhi::InputLayoutHandle m_InputLayout{};
		nvrhi::GraphicsPipelineHandle m_CascadePipeline{};
		nvrhi::GraphicsPipelineHandle m_AtlasPipeline{};
		nvrhi::FramebufferHandle m_CascadeFramebuffers[4]{};
		nvrhi::FramebufferHandle m_AtlasFramebuffer{};
		u32 m_BuiltShaderGeneration = ~0u;
		u32 m_BuiltShadowGeneration = ~0u;
	};
}
