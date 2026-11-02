#pragma once

#include "../render_pass.h"
#include "../shader_library.h"
#include "../viewport_target.h"

namespace wiwa
{
	// Temporal anti-aliasing: blends the HDR image of the frame (rendered with a jittered projection) with the previous result, found
	// again through the velocity target, and copies the result back into ViewportTarget::hdr(), so that exposure, bloom and tonemap are
	// unchanged. Does nothing unless the quality settings have TAA on and the scene has a camera.
	class TaaPass : public IRenderPass
	{
	public:
		virtual const char* get_name() const override { return "TAA"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override;
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;

	private:
		static constexpr u32 GROUP_SIZE = 8;

		// Mirrors TaaData in taa.hlsl.
		struct TaaData
		{
			glm::mat4 InverseViewProjection{ 1.0f };  // this frame, unjittered
			glm::mat4 PreviousViewProjection{ 1.0f }; // the previous frame, unjittered
			glm::vec4 Parameters{ 0.0f };             // x = feedback, y = 1 when the history can be used, z = width, w = height (pixels)
			glm::vec4 DepthInfo{ 0.0f };              // x = near plane, y = far plane, z = 1 for an orthographic camera
		};

		bool create_binding_sets(ViewportTarget& viewport);

		nvrhi::IDevice* m_Device = nullptr;
		ShaderLibrary* m_Shaders = nullptr;
		bool m_Failed = false;

		nvrhi::BufferHandle m_ConstantBuffer{};
		nvrhi::SamplerHandle m_Sampler{};
		nvrhi::BindingLayoutHandle m_BindingLayout{};
		nvrhi::BindingSetHandle m_BindingSets[2]{}; // [i]: reads history (1 - i), writes history i
		nvrhi::ComputePipelineHandle m_Pipeline{};
		u32 m_BuiltShaderGeneration = ~0u;
		u32 m_BuiltViewportGeneration = ~0u;

		u32 m_Current = 0;      // the history this frame writes
		bool m_HasRun = false;
		u64 m_LastRunFrame = 0; // the frame index of the last frame this pass resolved
		u32 m_LastRunGeneration = 0;
	};
}
