#pragma once

#include "../render_constants.h"
#include "../render_pass.h"
#include "pass_shaders.h"
#include "../view/per_view.h"
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
		virtual void on_view_removed(u32 targetUid) override { m_PerView.forget(targetUid); }
		virtual bool runs_in_view(const ViewFlags& flags, bool isMain) const override { return flags.Taa; }

	private:
		static constexpr u32 GROUP_SIZE = TAA_GROUP_SIZE;

		// Mirrors TaaData in taa.hlsl.
		struct TaaData
		{
			glm::mat4 InverseViewProjection{ 1.0f };  // this frame, unjittered
			glm::mat4 PreviousViewProjection{ 1.0f }; // the previous frame, unjittered
			glm::vec4 Parameters{ 0.0f };             // x = feedback, y = 1 when the history can be used, z = width, w = height (pixels)
			glm::vec4 DepthInfo{ 0.0f };              // x = near plane, y = far plane, z = 1 for an orthographic camera
		};

		nvrhi::IDevice* m_Device = nullptr;
		PassShaders m_Shaders{};
		bool m_Failed = false;

		nvrhi::BufferHandle m_ConstantBuffer{};
		nvrhi::SamplerHandle m_Sampler{};
		nvrhi::BindingLayoutHandle m_BindingLayout{};
		// What belongs to one view (PerView): its binding sets and temporal state. A view's slot starts over when its target is made again.
		struct ViewCache
		{
			nvrhi::BindingSetHandle BindingSets[2]{}; // [i]: reads history (1 - i), writes history i
			u32 Current = 0;      // the history this frame writes
			bool HasRun = false;
			u64 LastRunFrame = 0; // the frame index of the last frame this pass resolved
			u32 LastRunGeneration = 0;
			u32 TargetGeneration = 0; // of the history targets the binding sets were made for
		};
		PerView<ViewCache> m_PerView{};
		bool create_binding_sets(ViewportTarget& viewport, ViewCache& c);
		nvrhi::ComputePipelineHandle m_Pipeline{};

	};
}
