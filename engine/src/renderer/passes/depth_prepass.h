#pragma once

#include "../render_constants.h"
#include "../render_pass.h"
#include "../shader_library.h"
#include "scene_pass_utils.h"

namespace wiwa
{
	// Clears the viewport's depth and draws the opaque and alpha-masked geometry into it with their normals. The forward pass then shades
	// each pixel once.
	class DepthPrepass : public IRenderPass
	{
	public:
		virtual const char* get_name() const override { return "Depth prepass"; }

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

		nvrhi::InputLayoutHandle m_OpaqueInputLayout{};
		nvrhi::InputLayoutHandle m_MaskedInputLayout{};
		nvrhi::GraphicsPipelineHandle m_Opaque[2]{}; // [double sided]
		nvrhi::GraphicsPipelineHandle m_Masked[2]{};
		scene_pass::SurfacePipelines m_Surface{}; // materials with a surface shader (the Depth and DepthMasked variants)
		// The same for meshes that do not receive decals: they also write DECAL_STENCIL_BIT to the stencil, which the decal pass reads.
		nvrhi::GraphicsPipelineHandle m_OpaqueNoDecals[2]{};
		nvrhi::GraphicsPipelineHandle m_MaskedNoDecals[2]{};
		scene_pass::SurfacePipelines m_SurfaceNoDecals{};
		// DECAL_STENCIL_BIT (render_constants.h) is bit 2 of the stencil: the selection outline owns bits 0 and 1 (value 3) and runs later.
		u32 m_BuiltShaderGeneration = ~0u;
	};
}
