#pragma once

#include "../render_pass.h"
#include "../shader_library.h"

namespace wiwa
{
	// Draws the scene's sky behind the geometry: after the opaque forward stage, into the HDR target, where the depth is still the far
	// plane. With a solid background colour, no environment, or no camera it draws nothing (the forward pass cleared to the colour).
	class SkyPass : public IRenderPass
	{
	public:
		virtual const char* get_name() const override { return "Sky"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override;
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;

	private:
		nvrhi::IDevice* m_Device = nullptr;
		ShaderLibrary* m_Shaders = nullptr;
		bool m_Failed = false;

		nvrhi::GraphicsPipelineHandle m_Pipeline{};
		u32 m_BuiltShaderGeneration = ~0u;
		u32 m_BuiltViewportGeneration = ~0u;
	};
}
