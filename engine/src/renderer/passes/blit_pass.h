#pragma once

#include "../render_pass.h"
#include "../shader_library.h"

namespace wiwa
{
	// Draws the viewport's finished 8-bit image over the whole backbuffer, for a game that has no panel to show the scene in.
	// Does nothing while the scene is shown in a panel (FrameDescription::SceneInViewport).
	class BlitPass : public IRenderPass
	{
	public:
		virtual const char* get_name() const override { return "Blit"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override { return m_Pipeline != nullptr; }
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;
		virtual bool runs_in_view(const ViewFlags& flags, bool isMain) const override { return isMain; }

	private:
		nvrhi::IDevice* m_Device = nullptr;
		ShaderLibrary* m_Shaders = nullptr;
		bool m_Failed = false;

		nvrhi::SamplerHandle m_Sampler{};
		nvrhi::BindingLayoutHandle m_BindingLayout{};
		nvrhi::BindingSetHandle m_BindingSet{};
		const nvrhi::ITexture* m_BoundImage = nullptr;
		nvrhi::GraphicsPipelineHandle m_Pipeline{};
		u32 m_BuiltGeneration = ~0u;
	};
}
