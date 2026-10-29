#pragma once

#include "../render_pass.h"
#include "../shader_library.h"
#include "scene_pass_utils.h"

namespace wiwa
{
	// Clears the HDR target to the background and shades the geometry that the depth prepass left: PBR metallic-roughness lit by the
	// sun, the clustered local lights and a hemisphere ambient. Opaque and alpha-masked draws test against the prepass depth; alpha-blended
	// ones follow, sorted back to front, blended over the result without writing depth.
	class ForwardPass : public IRenderPass
	{
	public:
		// Opaque: clears the HDR target and draws opaque and alpha-masked geometry. Blended: draws the blended geometry over it (the
		// sky sits between the two).
		enum class Stage
		{
			Opaque,
			Blended,
		};

		explicit ForwardPass(Stage stage = Stage::Opaque) : m_Stage(stage) {}

		virtual const char* get_name() const override { return m_Stage == Stage::Opaque ? "Forward" : "Forward (blended)"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override;
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;

	private:
		void build_pipelines(const FrameDescription& frame);

		Stage m_Stage;
		nvrhi::IDevice* m_Device = nullptr;
		ShaderLibrary* m_Shaders = nullptr;
		bool m_Failed = false;

		nvrhi::InputLayoutHandle m_InputLayout{};
		nvrhi::GraphicsPipelineHandle m_Pipelines[2]{}; // [double sided]: opaque and alpha-masked
		nvrhi::GraphicsPipelineHandle m_BlendPipelines[2]{}; // alpha blended, no depth write
		u32 m_BuiltShaderGeneration = ~0u;
		u32 m_BuiltViewportGeneration = ~0u;
	};
}
