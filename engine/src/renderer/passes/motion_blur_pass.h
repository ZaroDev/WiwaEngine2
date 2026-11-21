#pragma once

#include "../render_constants.h"
#include "../render_pass.h"
#include "pass_shaders.h"
#include "../view/per_view.h"
#include "../viewport_target.h"

namespace wiwa
{
	// Motion blur: smears the HDR image along the velocity of every pixel (compute) and copies the result back into
	// ViewportTarget::hdr(). Runs while the scene has motion blur on, and for the velocity debug view
	// (SceneRenderData::set_debug_velocity), which replaces the image by the velocity.
	class MotionBlurPass : public IRenderPass
	{
	public:
		virtual const char* get_name() const override { return "Motion blur"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override;
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;
		virtual void on_view_removed(u32 targetUid) override { m_PerView.forget(targetUid); }
		virtual bool runs_in_view(const ViewFlags& flags, bool isMain) const override { return flags.MotionBlur; }

	private:
		static constexpr u32 GROUP_SIZE = MOTION_BLUR_GROUP_SIZE;

		// Mirrors MotionBlurData in motion_blur.hlsl.
		struct MotionBlurData
		{
			glm::mat4 InverseViewProjection{ 1.0f };  // this frame, unjittered
			glm::mat4 PreviousViewProjection{ 1.0f }; // the previous frame, unjittered
			glm::vec4 Parameters{ 0.0f };             // x = velocity scale, y = longest smear in pixels, z = width, w = height (pixels)
			glm::vec4 Mode{ 0.0f };                   // x = 1 for the velocity debug view
		};

		nvrhi::IDevice* m_Device = nullptr;
		PassShaders m_Shaders{};
		bool m_Failed = false;

		nvrhi::BufferHandle m_ConstantBuffer{};
		nvrhi::SamplerHandle m_Sampler{};
		nvrhi::BindingLayoutHandle m_BindingLayout{};
		// What belongs to one view (PerView): its binding sets. A view's slot starts over when its target is made again.
		struct ViewCache
		{
			nvrhi::BindingSetHandle BindingSet{};
			u32 TargetGeneration = 0; // of the scratch image the binding set was made for
		};
		PerView<ViewCache> m_PerView{};
		bool create_binding_set(ViewportTarget& viewport, ViewCache& c);
		nvrhi::ComputePipelineHandle m_Pipeline{};
	};
}
