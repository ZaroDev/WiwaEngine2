#pragma once

#include "../render_pass.h"
#include "../shader_library.h"
#include "../view/per_view.h"
#include "../viewport_target.h"

namespace wiwa
{
	// Ambient occlusion (GTAO) from the depth prepass's depth and normals, with three compute shaders: the distance of every pixel
	// along the view direction as a mip chain, the occlusion (a few directions searched around each pixel, with a different pattern
	// in every pixel of a 4x4 block) and a blur that averages the pattern away. The result is ViewportTarget::ao(), which the
	// forward pass reads. Does nothing while SceneRenderData::ao_active() is false.
	class AoPass : public IRenderPass
	{
	public:
		virtual const char* get_name() const override { return "AO"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override;
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;
		virtual void on_view_removed(u32 targetUid) override { m_PerView.forget(targetUid); }
		virtual bool runs_in_view(const ViewFlags& flags, bool isMain) const override { return flags.Ao; }

	private:
		static constexpr u32 GROUP_SIZE = 8;        // [numthreads] of gtao.hlsl and gtao_blur.hlsl
		static constexpr u32 DEPTH_GROUP_SIZE = 16; // and of gtao_depth.hlsl, which reduces 16x16 pixels to 5 levels

		// Mirrors the AoData cbuffer of the three shaders.
		struct AoData
		{
			glm::mat4 View{ 1.0f };           // world -> view space (the normals are in world space)
			glm::vec4 Projection{ 1.0f };     // x = P00, y = P11, z = clip-space Y sign, w = last mip level of the distances
			glm::vec4 DepthInfo{ 1.0f };      // x = near plane, y = far plane, z = width, w = height (pixels)
			glm::vec4 Settings{ 1.0f };       // x = radius (world units), y = power, z = slices, w = steps
		};

		nvrhi::IDevice* m_Device = nullptr;
		ShaderLibrary* m_Shaders = nullptr;
		bool m_Failed = false;

		nvrhi::BufferHandle m_ConstantBuffer{};
		// A viewport too small for all the levels of the distances binds these to the slots of the missing ones: D3D11 does not allow two
		// unordered access views of the same subresource at once.
		nvrhi::TextureHandle m_UnusedLevels[ViewportTarget::MAX_AO_DEPTH_LEVELS - 1]{};
		nvrhi::BindingLayoutHandle m_DepthLayout{};
		nvrhi::BindingLayoutHandle m_AoLayout{};
		nvrhi::BindingLayoutHandle m_BlurLayout{};
		// What belongs to one view (PerView): its binding sets. A view's slot starts over when its target is made again.
		struct ViewCache
		{
			nvrhi::BindingSetHandle DepthSet{};
			nvrhi::BindingSetHandle AoSet{};
			nvrhi::BindingSetHandle BlurSet{};
		};
		PerView<ViewCache> m_PerView{};
		bool create_binding_sets(ViewportTarget& viewport, ViewCache& c);
		nvrhi::ComputePipelineHandle m_DepthPipeline{};
		nvrhi::ComputePipelineHandle m_AoPipeline{};
		nvrhi::ComputePipelineHandle m_BlurPipeline{};
		u32 m_BuiltShaderGeneration = ~0u;
	};
}
