#pragma once

#include "../render_pass.h"
#include "../shader_library.h"
#include "scene_pass_utils.h"

namespace wiwa
{
	// Draws the blended items into ViewportTarget::velocity() where their alpha is 0.5 or more (the depth prepass left them out), tested
	// against the opaque depth, so that TAA and motion blur follow moving glass instead of what is behind it.
	class VelocityPass : public IRenderPass
	{
	public:
		virtual const char* get_name() const override { return "Transparent velocity"; }

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
		scene_pass::SurfacePipelines m_Surface{}; // materials with a surface shader (the Velocity variant)
		u32 m_BuiltShaderGeneration = ~0u;
		u32 m_BuiltViewportGeneration = ~0u;
	};
}
