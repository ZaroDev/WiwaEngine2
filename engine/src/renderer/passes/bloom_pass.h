#pragma once

#include <array>

#include "../render_pass.h"
#include "../shader_library.h"
#include "../viewport_target.h"

namespace wiwa
{
	// Bloom: keeps what is brighter than the scene's threshold, blurs it by halving the HDR image several times and spreading it back
	// up, into the viewport's bloom chain (compute shaders). The tonemap pass adds ViewportTarget::bloom() to the image. Does nothing
	// while the scene has bloom off.
	class BloomPass : public IRenderPass
	{
	public:
		virtual const char* get_name() const override { return "Bloom"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override;
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;

	private:
		static constexpr u32 GROUP_SIZE = 8;

		// Mirrors BloomDownData in bloom_down.hlsl.
		struct DownConstants
		{
			float Threshold = 1.0f;
			float Knee = 0.5f;
			float FirstStep = 0.0f;
			float Padding0 = 0.0f;
			float TexelX = 0.0f; // 1 / the size of the image that is read
			float TexelY = 0.0f;
			float Padding1[2]{};
		};

		bool create_binding_sets(ViewportTarget& viewport);

		nvrhi::IDevice* m_Device = nullptr;
		ShaderLibrary* m_Shaders = nullptr;
		bool m_Failed = false;

		nvrhi::SamplerHandle m_Sampler{};
		nvrhi::BindingLayoutHandle m_DownLayout{};
		nvrhi::BindingLayoutHandle m_UpLayout{};
		nvrhi::ComputePipelineHandle m_DownPipeline{};
		nvrhi::ComputePipelineHandle m_UpPipeline{};
		std::array<nvrhi::BindingSetHandle, ViewportTarget::MAX_BLOOM_MIPS> m_DownSets{};          // [i]: reads level i - 1 (the scene for 0), writes down(i)
		std::array<nvrhi::BindingSetHandle, ViewportTarget::MAX_BLOOM_MIPS - 1> m_UpSets{};        // [i]: down(i) + up(i + 1) spread -> up(i)
		std::array<nvrhi::BindingSetHandle, ViewportTarget::MAX_BLOOM_MIPS - 1> m_UpDeepestSets{}; // [i]: down(i) + down(i + 1) spread -> up(i)
		u32 m_BuiltShaderGeneration = ~0u;
		u32 m_BuiltViewportGeneration = ~0u;
	};
}
