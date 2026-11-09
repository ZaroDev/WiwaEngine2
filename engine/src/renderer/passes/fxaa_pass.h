#pragma once

#include "../render_pass.h"
#include "../shader_library.h"
#include "../view/per_view.h"

namespace wiwa
{
	// FXAA on the tone-mapped image: reads ViewportTarget::pre_fxaa() (where the tonemap pass drew) and writes ldr(). Does nothing
	// while the quality settings have FXAA off; the tonemap pass then drew into ldr() itself.
	class FxaaPass : public IRenderPass
	{
	public:
		virtual const char* get_name() const override { return "FXAA"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override;
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;
		virtual void on_view_removed(u32 targetUid) override { m_PerView.forget(targetUid); }
		virtual bool runs_in_view(const ViewFlags& flags, bool isMain) const override { return flags.Fxaa; }

	private:
		// Mirrors FxaaData in fxaa.hlsl.
		struct PushConstants
		{
			float RcpWidth = 1.0f;
			float RcpHeight = 1.0f;
			float Transparent = 0.0f; // 1: straight alpha in the image
			float Padding = 0.0f;
		};

		nvrhi::IDevice* m_Device = nullptr;
		ShaderLibrary* m_Shaders = nullptr;
		bool m_Failed = false;

		nvrhi::SamplerHandle m_Sampler{};
		nvrhi::BindingLayoutHandle m_BindingLayout{};
		// What belongs to one view (PerView): its binding sets. A view's slot starts over when its target is made again.
		struct ViewCache
		{
			nvrhi::BindingSetHandle BindingSet{};
		};
		PerView<ViewCache> m_PerView{};
		nvrhi::GraphicsPipelineHandle m_Pipeline{};
		u32 m_BuiltShaderGeneration = ~0u;
	};
}
