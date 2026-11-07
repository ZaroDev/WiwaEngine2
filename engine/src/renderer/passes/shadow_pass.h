#pragma once

#include "../render_pass.h"
#include "../shader_library.h"
#include "../view/per_view.h"
#include "scene_pass_utils.h"

namespace wiwa
{
	// Draws the shadow maps the scene data lists for this frame: the sun's cascades (slices of a depth array) and the spot lights'
	// tiles of an atlas. Depth only, no culling of back faces; the forward pass samples the maps.
	class ShadowPass : public IRenderPass
	{
	public:
		virtual const char* get_name() const override { return "Shadows"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override;
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;
		virtual void on_view_removed(u32 targetUid) override { m_PerView.forget(targetUid); }
		virtual bool runs_in_view(const ViewFlags& flags, bool isMain) const override { return flags.ShadowCascades > 0 || flags.SpotShadows; }

	private:
		// What belongs to one view: its shadow maps are its own, so are the framebuffers on them and the set that reads its instances.
		struct ViewCache
		{
			u32 ShadowGeneration = ~0u;
			nvrhi::FramebufferHandle CascadeFramebuffers[4]{};
			nvrhi::FramebufferHandle AtlasFramebuffer{};
			nvrhi::BindingSetHandle BindingSet{};
			const nvrhi::IBuffer* BoundInstances = nullptr;
		};
		void rebuild(const SceneRenderData& data, ViewCache& cache);

		nvrhi::IDevice* m_Device = nullptr;
		ShaderLibrary* m_Shaders = nullptr;
		bool m_Failed = false;

		nvrhi::BindingLayoutHandle m_Layout{};
		nvrhi::InputLayoutHandle m_InputLayout{};
		nvrhi::GraphicsPipelineHandle m_CascadePipeline{};
		nvrhi::GraphicsPipelineHandle m_AtlasPipeline{};
		scene_pass::SurfacePipelines m_SurfaceCascade{}; // casters with a surface shader (the Shadow and ShadowMasked variants)
		scene_pass::SurfacePipelines m_SurfaceAtlas{};
		u32 m_BuiltShaderGeneration = ~0u;
		PerView<ViewCache> m_PerView{};
	};
}
