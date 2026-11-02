#pragma once

#include <unordered_map>

#include "../render_pass.h"
#include "../shader_library.h"

namespace wiwa
{
	// Draws the game UI (FrameDescription::Ui, built by the UiModule) over the finished LDR image, after FXAA, so that exposure, bloom, TAA
	// and tone mapping never change its colours. Colours are written as they are: the LDR image holds display values. A batch whose font
	// or texture is not on the GPU yet is skipped. Does nothing for a frame without UI or without a scene to draw it over.
	class UiRenderPass : public IRenderPass
	{
	public:
		virtual const char* get_name() const override { return "UI"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override;
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;

	private:
		// Mirrors PushConstantsData in ui.hlsl.
		struct PushConstants
		{
			float Scale[2]{};
			float Translate[2]{};
			float Params[4]{};
			float OutlineColor[4]{};
			float Extra[4]{};
		};

		struct BoundTexture
		{
			nvrhi::TextureHandle Texture{}; // held so that the address cannot be reused by another texture while the set is cached
			nvrhi::BindingSetHandle Set{};
		};

		bool build_pipeline(nvrhi::IFramebuffer* framebuffer);
		bool ensure_buffers(u32 vertexCount, u32 indexCount);
		nvrhi::IBindingSet* binding_set_for(nvrhi::ITexture* texture);

		nvrhi::IDevice* m_Device = nullptr;
		ShaderLibrary* m_Shaders = nullptr;
		bool m_Failed = false;

		nvrhi::BindingLayoutHandle m_BindingLayout{};
		nvrhi::SamplerHandle m_Sampler{};
		nvrhi::TextureHandle m_White{};
		nvrhi::InputLayoutHandle m_InputLayout{};
		nvrhi::GraphicsPipelineHandle m_Pipeline{};
		std::unordered_map<nvrhi::ITexture*, BoundTexture> m_BindingSets{};
		nvrhi::BufferHandle m_VertexBuffer{};
		nvrhi::BufferHandle m_IndexBuffer{};
		u32 m_VertexCapacity = 0;
		u32 m_IndexCapacity = 0;
		u32 m_BuiltShaderGeneration = ~0u;
		u32 m_BuiltViewportGeneration = ~0u;
	};
}
