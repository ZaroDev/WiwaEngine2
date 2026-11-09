#pragma once

#include "../particles/particle_pool.h"
#include "../render_pass.h"
#include "../shader_library.h"
#include "../view/per_view.h"
#include "../viewport_target.h"

namespace wiwa
{
	// Draws the sprite particles of the frame (ParticlePool::Draws, in order) into the HDR image: one draw per emitter, one instance per slot of its ring.
	// No depth attachment: the shader reads the scene depth itself (particle_sprite.hlsl). Runs after the blended forward items, in the main view.
	class ParticleRenderPass : public IRenderPass
	{
	public:
		virtual const char* get_name() const override { return "Particles"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override;
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;
		virtual void on_view_removed(u32 targetUid) override { m_PerView.forget(targetUid); }
		virtual bool runs_in_view(const ViewFlags& flags, bool isMain) const override { return isMain; }

	private:
		// What belongs to one view (PerView): the set that reads the pool and the view's depth. Made again when the pool is.
		struct ViewCache
		{
			nvrhi::BindingSetHandle BindingSet{};
			u32 PoolGeneration = ~0u;
			u32 SortGeneration = ~0u;
		};

		nvrhi::IDevice* m_Device = nullptr;
		ShaderLibrary* m_Shaders = nullptr;
		ParticlePool* m_Pool = nullptr;
		bool m_Failed = false;

		nvrhi::BindingLayoutHandle m_Layout{}; // set 2: the particles, the emitters, the tables, the scene depth
		PerView<ViewCache> m_PerView{};
		nvrhi::GraphicsPipelineHandle m_Pipelines[3]{}; // by ParticleBlend
		u32 m_BuiltShaderGeneration = ~0u;
	};
}
