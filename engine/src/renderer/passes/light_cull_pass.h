#pragma once

#include "../render_pass.h"
#include "pass_shaders.h"
#include "../view/per_view.h"

namespace wiwa
{
	// Sorts the scene's point and spot lights into the cluster grid (16 x 9 screen tiles times 24 exponential depth slices of the
	// camera's view) with a compute shader: for every cluster, the indices of the lights that reach it. The forward pass reads the
	// lists. Does nothing without lights or a camera.
	class LightCullPass : public IRenderPass
	{
	public:
		virtual const char* get_name() const override { return "Light culling"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override;
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;
		virtual void on_view_removed(u32 targetUid) override { m_PerView.forget(targetUid); }
		virtual bool runs_in_view(const ViewFlags& flags, bool isMain) const override { return flags.LightCulling; }

	private:
		nvrhi::IDevice* m_Device = nullptr;
		PassShaders m_Shaders{};
		bool m_Failed = false;

		nvrhi::BindingLayoutHandle m_Layout{};
		// The set reads the view's own constant, light and cluster buffers.
		struct ViewCache
		{
			nvrhi::BindingSetHandle BindingSet{};
		};
		PerView<ViewCache> m_PerView{};
		nvrhi::ComputePipelineHandle m_Pipeline{};
	};
}
