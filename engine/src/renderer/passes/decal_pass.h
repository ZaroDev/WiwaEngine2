#pragma once

#include "../render_pass.h"
#include "../shader_library.h"
#include "../view/per_view.h"
#include "../viewport_target.h"

namespace wiwa
{
	// Decals (decals.md): draws every visible decal of the view (SceneRenderData::visible_decals(), already culled and sorted) as a box that
	// projects its material onto what the depth prepass drew, into the four decal targets of the viewport (premultiplied color, octahedral
	// normal, occlusion / roughness / metallic, emissive light). The forward shaders read them where they build a surface. Does nothing
	// while the view has no visible decals (the targets then do not exist either).
	class DecalPass : public IRenderPass
	{
	public:
		virtual const char* get_name() const override { return "Decals"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override;
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;
		virtual void on_view_removed(u32 targetUid) override { m_PerView.forget(targetUid); }
		virtual bool runs_in_view(const ViewFlags& flags, bool isMain) const override { return flags.Decals; }

		// What the shader reads per decal (DecalRecord in decal.hlsl): the unit cube's matrix and its inverse as column vectors, the way
		// the decal projects and its opacity, the fades and the channels.
		struct DecalGpu
		{
			glm::vec4 Model[4]{};
			glm::vec4 Inverse[4]{};
			glm::vec4 Axis{ 0.0f, 0.0f, -1.0f, 1.0f };
			glm::vec4 Fade{ 0.0f };
			glm::uvec4 Info{ 0u };
		};

	private:
		nvrhi::IDevice* m_Device = nullptr;
		ShaderLibrary* m_Shaders = nullptr;
		bool m_Failed = false;

		nvrhi::BindingLayoutHandle m_Layout{}; // set 2: the prepass depth, its stencil and normals, the decals
		// What belongs to one view (PerView): the records of its decals and the set that reads them with the view's depth, stencil and normals.
		struct ViewCache
		{
			nvrhi::BufferHandle Records{};
			nvrhi::BindingSetHandle BindingSet{};
		};
		PerView<ViewCache> m_PerView{};
		bool create_view_cache(ViewportTarget& viewport, ViewCache& cache);

		nvrhi::GraphicsPipelineHandle m_Pipeline{};
		u32 m_BuiltShaderGeneration = ~0u;
	};
}
