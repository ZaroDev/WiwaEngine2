#pragma once

#include <unordered_map>

#include <ui/ui_draw_list.h>

#include "../render_pass.h"
#include "../shader_library.h"

namespace wiwa
{
	class GpuResourceCache;

	// Draws the game UI (FrameDescription::Ui, built by the UiModule) over the finished LDR image, after FXAA, so that exposure, bloom, TAA
	// and tone mapping never change its colours. Colours are written as they are: the LDR image holds display values. A batch whose font
	// or texture is not on the GPU yet is skipped. Does nothing for a frame without UI or without a scene to draw it over.
	//
	// It also draws the UI editor's canvas: a list of its own into a texture of its own, every frame, with the same code and shader (see
	// set_preview). That one needs no scene.
	class UiRenderPass : public IRenderPass
	{
	public:
		virtual const char* get_name() const override { return "UI"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override;
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;
		virtual bool runs_in_view(const ViewFlags& flags, bool isMain) const override { return isMain; }

		// Render thread (a command from RenderModule::set_ui_preview): from now on every frame clears a texture of this size to the
		// background and draws the list into it. The texture is registered as UI_PREVIEW_TEXTURE_ID, for ImGui to show. The list is in
		// canvas pixels of list.CanvasSize, scaled to the texture. A width or height of 0 is clear_preview().
		void set_preview(UiDrawList list, u32 width, u32 height, const glm::vec4& background);
		// Stops the preview and lets go of its texture (its id is not registered any more).
		void clear_preview();

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

		// A pipeline is made for the framebuffer it draws into, and again when the shaders or that framebuffer were replaced.
		struct PipelineSlot
		{
			nvrhi::GraphicsPipelineHandle Pipeline{};
			u32 ShaderGeneration = ~0u;
			u32 TargetGeneration = ~0u;
		};

		bool build_pipeline(nvrhi::IFramebuffer* framebuffer, PipelineSlot& slot);
		bool ensure_buffers(u32 vertexCount, u32 indexCount);
		nvrhi::IBindingSet* binding_set_for(nvrhi::ITexture* texture);

		// Records the draws of a list into a framebuffer of this size. False if the pass cannot go on (it is then failed).
		bool draw_list(nvrhi::ICommandList* commandList, const UiDrawList& list, nvrhi::IFramebuffer* framebuffer, u32 width, u32 height,
			u32 targetGeneration, GpuResourceCache& cache, PipelineSlot& slot);

		nvrhi::IDevice* m_Device = nullptr;
		ShaderLibrary* m_Shaders = nullptr;
		TextureRegistry* m_Textures = nullptr;
		GpuResourceCache* m_Cache = nullptr;
		bool m_Failed = false;

		nvrhi::BindingLayoutHandle m_BindingLayout{};
		nvrhi::SamplerHandle m_Sampler{};
		nvrhi::TextureHandle m_White{};
		nvrhi::InputLayoutHandle m_InputLayout{};
		PipelineSlot m_ViewportSlot{};
		std::unordered_map<nvrhi::ITexture*, BoundTexture> m_BindingSets{};
		nvrhi::BufferHandle m_VertexBuffer{};
		nvrhi::BufferHandle m_IndexBuffer{};
		u32 m_VertexCapacity = 0;
		u32 m_IndexCapacity = 0;

		// the editor's canvas
		UiDrawList m_PreviewList{};
		glm::vec4 m_PreviewBackground{ 0.0f };
		u32 m_PreviewWidth = 0;
		u32 m_PreviewHeight = 0;
		u32 m_PreviewGeneration = 0; // changes with the texture
		nvrhi::TextureHandle m_PreviewTexture{};
		nvrhi::FramebufferHandle m_PreviewFramebuffer{};
		PipelineSlot m_PreviewSlot{};
	};
}
