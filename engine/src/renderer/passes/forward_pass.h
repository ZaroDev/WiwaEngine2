#pragma once

#include "../render_pass.h"
#include "../shader_library.h"
#include "scene_pass_utils.h"

namespace wiwa
{
	// Clears the HDR target to the background and shades the geometry that the depth prepass left: PBR metallic-roughness lit by the
	// sun and a hemisphere ambient. Opaque and alpha-masked draws test against the prepass depth.
	class ForwardPass : public IRenderPass
	{
	public:
		virtual const char* get_name() const override { return "Forward"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override;
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;

	private:
		void build_pipelines(const FrameDescription& frame);

		nvrhi::IDevice* m_Device = nullptr;
		ShaderLibrary* m_Shaders = nullptr;
		bool m_Failed = false;

		nvrhi::InputLayoutHandle m_InputLayout{};
		nvrhi::GraphicsPipelineHandle m_Pipelines[2]{}; // [double sided]
		u32 m_BuiltShaderGeneration = ~0u;
		u32 m_BuiltViewportGeneration = ~0u;
	};
}
