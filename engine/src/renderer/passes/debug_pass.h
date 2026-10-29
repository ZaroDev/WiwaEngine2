#pragma once

#include <vector>

#include "../debug/debug_draw.h"
#include "../render_pass.h"
#include "../shader_library.h"

namespace wiwa
{
	// Draws the frame's debug lines and triangles (FrameDescription::Debug) over the finished LDR image, after FXAA, so bloom, exposure
	// and tone mapping never change their colors. The depth-tested layer reads the scene depth through the LDR + depth framebuffer; the
	// always-on-top layer ignores it. Does nothing for a frame without shapes.
	class DebugPass : public IRenderPass
	{
	public:
		virtual const char* get_name() const override { return "Debug draw"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override;
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;

	private:
		// Mirrors DebugData in debug_common.hlsli.
		struct PushConstants
		{
			glm::mat4 ViewProjection{ 1.0f };
			glm::vec2 Viewport{ 1.0f };
			u32 FirstElement = 0;
			u32 Padding = 0;
		};

		bool build_pipelines(nvrhi::IFramebuffer* framebuffer);
		bool ensure_buffers(u32 lineCount, u32 triangleCount);

		nvrhi::IDevice* m_Device = nullptr;
		ShaderLibrary* m_Shaders = nullptr;
		bool m_Failed = false;

		nvrhi::BindingLayoutHandle m_BindingLayout{};
		nvrhi::BindingSetHandle m_BindingSet{};
		nvrhi::GraphicsPipelineHandle m_LinePipelines[2]{};     // [0] depth tested, [1] always on top
		nvrhi::GraphicsPipelineHandle m_TrianglePipelines[2]{};
		nvrhi::BufferHandle m_LineBuffer{};
		nvrhi::BufferHandle m_TriangleBuffer{};
		u32 m_LineCapacity = 0;
		u32 m_TriangleCapacity = 0;
		std::vector<DebugLine> m_LineScratch{};         // both layers, tested first
		std::vector<DebugTriangle> m_TriangleScratch{};
		u32 m_BuiltShaderGeneration = ~0u;
		u32 m_BuiltViewportGeneration = ~0u;
	};
}
