#pragma once

#include "../render_pass.h"
#include "../shader_library.h"

namespace wiwa
{
	// Turns the viewport's HDR color into its 8-bit image: exposure from the scene settings, the tone mapping operator, color grading
	// and the sRGB encoding, drawn as one fullscreen triangle.
	class TonemapPass : public IRenderPass
	{
	public:
		virtual const char* get_name() const override { return "Tonemap"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override;
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;

	private:
		struct PushConstants
		{
			float Exposure = 1.0f;
			u32 Operator = 0;
			float Contrast = 1.0f;
			float Saturation = 1.0f;
			float Temperature = 0.0f;
			float Tint = 0.0f;
			float Vignette = 0.0f;
			float AutoExposure = 0.0f; // 1: the exposure comes from the viewport's exposure buffer
			float BloomIntensity = 0.0f; // 0: the bloom is not read
			float Padding[3]{};
		};

		nvrhi::IDevice* m_Device = nullptr;
		ShaderLibrary* m_Shaders = nullptr;
		bool m_Failed = false;

		nvrhi::SamplerHandle m_Sampler{};
		nvrhi::SamplerHandle m_LinearSampler{}; // the bloom is a smaller image
		nvrhi::BindingLayoutHandle m_BindingLayout{};
		nvrhi::BindingSetHandle m_BindingSet{};
		nvrhi::GraphicsPipelineHandle m_Pipeline{};
		u32 m_BuiltShaderGeneration = ~0u;
		u32 m_BuiltViewportGeneration = ~0u;
	};
}
