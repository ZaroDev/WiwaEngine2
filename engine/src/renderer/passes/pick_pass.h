#pragma once

#include <optional>

#include "../debug/debug_draw.h"
#include "../render_pass.h"
#include "../render_pick.h"
#include "../shader_library.h"
#include "scene_pass_utils.h"

namespace wiwa
{
	// Answers the pending pick request (PickState::Pending) with an ID buffer. On a frame that has one, it draws every visible instance
	// (value: instance slot + 1) and every pick icon of the frame (0x80000000 | its index in DebugDrawData::PickIcons) into an R32_UINT
	// target with a depth buffer of its own, copies the clicked pixel to the CPU after the frame is submitted and publishes the entity
	// found. The target exists only for that frame. Frames without a request cost nothing.
	class PickPass : public IRenderPass
	{
	public:
		static constexpr u32 ICON_ID_BIT = 0x80000000u;

		virtual const char* get_name() const override { return "Pick"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override;
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;
		virtual void on_frame_submitted(nvrhi::IDevice* device, const FrameDescription& frame) override;

	private:
		// Mirrors PickIcon in pick_icon.hlsl.
		struct PickIconGpu
		{
			glm::vec3 Position{ 0.0f };
			float RadiusPx = 0.0f;
			u32 Id = 0;
			u32 Padding[3]{};
		};
		static_assert(sizeof(PickIconGpu) == 32);

		// Mirrors IconData in pick_icon.hlsl.
		struct IconConstants
		{
			glm::mat4 ViewProjection{ 1.0f };
			glm::vec2 Viewport{ 1.0f };
			float Padding[2]{};
		};

		bool build_pipelines(const SceneRenderData& data, nvrhi::IFramebuffer* framebuffer);
		bool create_target(u32 width, u32 height);
		void release_target();

		nvrhi::IDevice* m_Device = nullptr;
		ShaderLibrary* m_Shaders = nullptr;
		bool m_Failed = false;

		nvrhi::InputLayoutHandle m_InputLayout{};
		nvrhi::GraphicsPipelineHandle m_InstancePipelines[2]{}; // [double sided]
		nvrhi::BindingLayoutHandle m_IconLayout{};
		nvrhi::GraphicsPipelineHandle m_IconPipeline{};
		nvrhi::BindingSetHandle m_IconSet{};
		nvrhi::BufferHandle m_IconBuffer{};
		u32 m_IconCapacity = 0;
		std::vector<PickIconGpu> m_IconScratch{};
		u32 m_BuiltShaderGeneration = ~0u;

		// Alive only while a request is being answered.
		nvrhi::TextureHandle m_IdTarget{};
		nvrhi::TextureHandle m_IdDepth{};
		nvrhi::FramebufferHandle m_IdFramebuffer{};
		nvrhi::StagingTextureHandle m_Staging{};
		bool m_Recorded = false; // this frame's commands copy the clicked pixel
	};
}
