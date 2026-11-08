#pragma once

#include <unordered_map>
#include <vector>

#include "../render_pass.h"
#include "../shader_library.h"
#include "../texture_registry.h"

namespace wiwa
{
	// Draws the UI the main thread built (a copy of the ImGui draw data in the batch) on top of the backbuffer. Added last,
	// so it overlays the scene. The shaders are the cooked engine/shaders/imgui.hlsl.
	class ImGuiRenderPass : public IRenderPass
	{
	public:
		virtual const char* get_name() const override { return "ImGui"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override { return m_Pipeline != nullptr && m_FontTexture != nullptr; }
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;
		virtual bool runs_in_view(const ViewFlags& flags, bool isMain) const override { return isMain; }

		// Render thread: creates the font texture from the atlas the main thread built (RGBA8). Called by a render command.
		void set_font_atlas(u32 width, u32 height, const u8* pixels);
		// The texture is going away: its binding set (which holds it) is dropped.
		void forget_texture(nvrhi::ITexture* texture) { m_BindingSets.erase(texture); }

	private:
		struct PushConstants
		{
			float Scale[2]{};
			float Translate[2]{};
		};

		void ensure_buffer_capacity(u32 vertexCount, u32 indexCount);
		nvrhi::IBindingSet* binding_set_for(nvrhi::ITexture* texture);

		nvrhi::IDevice* m_Device = nullptr;
		ShaderLibrary* m_Shaders = nullptr;
		bool m_Failed = false;

		nvrhi::InputLayoutHandle m_InputLayout{};
		nvrhi::BindingLayoutHandle m_BindingLayout{};
		TextureRegistry* m_Textures = nullptr;
		std::unordered_map<nvrhi::ITexture*, nvrhi::BindingSetHandle> m_BindingSets{}; // one per texture the UI draws
		nvrhi::GraphicsPipelineHandle m_Pipeline{};
		nvrhi::SamplerHandle m_Sampler{};
		nvrhi::TextureHandle m_FontTexture{};
		u32 m_BuiltGeneration = ~0u; // the shader generation the pipeline was built from

		nvrhi::BufferHandle m_VertexBuffer{};
		nvrhi::BufferHandle m_IndexBuffer{};
		u32 m_VertexCapacity = 0;
		u32 m_IndexCapacity = 0;
	};
}
