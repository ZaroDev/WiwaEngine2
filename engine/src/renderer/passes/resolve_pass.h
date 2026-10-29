#pragma once

#include "../render_pass.h"
#include "../shader_library.h"

namespace wiwa
{
	// Turns the viewport's HDR color into its 8-bit image: exposure from the scene settings, the ACES filmic curve and the sRGB
	// encoding, drawn as one fullscreen triangle. A plain stand-in for the post-processing chain that comes later.
	class ResolvePass : public IRenderPass
	{
	public:
		virtual const char* get_name() const override { return "Resolve"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override;
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;

	private:
		struct PushConstants
		{
			float Exposure = 1.0f;
			float Padding[3]{};
		};

		nvrhi::IDevice* m_Device = nullptr;
		ShaderLibrary* m_Shaders = nullptr;
		bool m_Failed = false;

		nvrhi::SamplerHandle m_Sampler{};
		nvrhi::BindingLayoutHandle m_BindingLayout{};
		nvrhi::BindingSetHandle m_BindingSet{};
		nvrhi::GraphicsPipelineHandle m_Pipeline{};
		u32 m_BuiltShaderGeneration = ~0u;
		u32 m_BuiltViewportGeneration = ~0u;
	};
}
