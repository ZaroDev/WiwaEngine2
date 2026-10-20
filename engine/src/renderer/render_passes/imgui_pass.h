#pragma once

#include <renderer/render_pass.h>
#include <renderer/window.h>

#include "imgui.h"

#include <functional>

namespace wiwa
{
	struct ImGuiPassDescription
	{
		GLFWwindow* Window = nullptr;
		std::function<void()> BuildUi; // called every frame between NewFrame and Render
	};

	class ImGuiPass : public IRenderPass
	{
	public:
		explicit ImGuiPass(const ImGuiPassDescription& description);
		virtual ~ImGuiPass();

		virtual bool initialize(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual void render(nvrhi::ICommandList* command_list, const FrameDescription& frame) override;

	private:
		struct PushConstants
		{
			float Scale[2];
			float Translate[2];
		};

		bool create_font_texture(nvrhi::IDevice* device);
		void ensure_buffer_capacity(uint32_t vertex_count, uint32_t index_count);

	private:
		ImGuiPassDescription m_Description;
		nvrhi::IDevice* m_Device = nullptr;
		bool m_ContextCreated = false;

		nvrhi::ShaderHandle m_VertexShader;
		nvrhi::ShaderHandle m_PixelShader;
		nvrhi::InputLayoutHandle m_InputLayout;
		nvrhi::BindingLayoutHandle m_BindingLayout;
		nvrhi::BindingSetHandle m_BindingSet;
		nvrhi::GraphicsPipelineHandle m_Pipeline;
		nvrhi::SamplerHandle m_Sampler;
		nvrhi::TextureHandle m_FontTexture;

		nvrhi::BufferHandle m_VertexBuffer;
		nvrhi::BufferHandle m_IndexBuffer;
		uint32_t m_VertexCapacity = 0;
		uint32_t m_IndexCapacity = 0;

		std::vector<ImDrawVert> m_Vertices; // CPU staging, reused every frame
		std::vector<ImDrawIdx> m_Indices;
	};
}