#pragma once

#include "../render_pass.h"
#include "../shader_library.h"
#include "../view/per_view.h"

namespace wiwa
{
	// Automatic exposure: a compute pass counts the HDR scene's luminance into a 256-bin histogram, a second one averages the middle
	// of it and moves the viewport's exposure buffer towards the exposure that shows the average at the scene's key brightness. The
	// tonemap pass reads the buffer. Does nothing while the scene's exposure is manual.
	class ExposurePass : public IRenderPass
	{
	public:
		// The histogram covers 2^-10 .. 2^6; the same numbers are in exposure_histogram.hlsl and exposure_resolve.hlsl.
		static constexpr float MIN_LOG2 = -10.0f;
		static constexpr float RANGE_LOG2 = 16.0f;
		// The longest step the adaptation takes, in seconds: a stall (a shader compile) must not jump the exposure.
		static constexpr float MAX_DELTA = 0.1f;

		virtual const char* get_name() const override { return "Exposure"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override;
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;
		virtual void on_view_removed(u32 targetUid) override { m_PerView.forget(targetUid); }
		virtual bool runs_in_view(const ViewFlags& flags, bool isMain) const override { return flags.AutoExposure; }

	private:
		struct HistogramConstants
		{
			float MinLog2 = MIN_LOG2;
			float RangeLog2 = RANGE_LOG2;
			float Padding[2]{};
		};

		struct ResolveConstants
		{
			float MinLog2 = MIN_LOG2;
			float RangeLog2 = RANGE_LOG2;
			float LowShare = 0.1f;
			float HighShare = 0.9f;
			float Key = 0.3f;
			float CompensationEv = 0.0f;
			float MinEv = -4.0f;
			float MaxEv = 4.0f;
			float DeltaSeconds = 0.0f;
			float Speed = 1.5f;
			float Reset = 1.0f;
			float Padding = 0.0f;
		};

		nvrhi::IDevice* m_Device = nullptr;
		ShaderLibrary* m_Shaders = nullptr;
		bool m_Failed = false;

		nvrhi::BindingLayoutHandle m_HistogramLayout{};
		nvrhi::BindingLayoutHandle m_ResolveLayout{};
		// What belongs to one view (PerView): its binding sets and temporal state. A view's slot starts over when its target is made again.
		struct ViewCache
		{
			nvrhi::BindingSetHandle HistogramSet{};
			nvrhi::BindingSetHandle ResolveSet{};
			bool WasActive = false;    // the previous frame measured the exposure
			u32 LastSceneResets = ~0u; // the scene the history belongs to
		};
		PerView<ViewCache> m_PerView{};
		nvrhi::ComputePipelineHandle m_HistogramPipeline{};
		nvrhi::ComputePipelineHandle m_ResolvePipeline{};
		u32 m_BuiltShaderGeneration = ~0u;

	};
}
