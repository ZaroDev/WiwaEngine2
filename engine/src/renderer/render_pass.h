#pragma once

#include <nvrhi/nvrhi.h>

#include <math/math.h>

#include <ui/ui_draw_list.h>

#include "imgui_draw_data.h"

namespace wiwa
{
	class ShaderLibrary;
	class TextureRegistry;
	class GpuResourceCache;
	class SceneRenderData;
	struct DebugDrawData;
	struct PickState;

	// Everything a pass needs to know about the current frame.
	struct FrameDescription
	{
		nvrhi::IFramebuffer* Backbuffer = nullptr;
		u32 Width = 0;
		u32 Height = 0;
		float Time = 0.0f;
		float DeltaTime = 0.0f; // seconds since the last rendered frame
		u64 FrameIndex = 0; // counts rendered frames
		const OwnedDrawData* ImGui = nullptr; // the UI of this frame, built on the main thread
		bool SceneInViewport = false; // the UI shows the scene in a panel; otherwise it fills the window (the blit pass draws it)
		PickState* Pick = nullptr; // the click waiting to be answered, and where its answer goes
		const DebugDrawData* Debug = nullptr; // the debug shapes of this frame (null when there are none)
		const UiDrawList* Ui = nullptr; // the game UI of this frame (null when there is none)
		const SceneRenderData* Scene = nullptr; // the scene's draw lists and buffers for this frame (null when there is nothing to draw)
	};

	struct PassInitializationDescription
	{
		nvrhi::IDevice* Device = nullptr;
		nvrhi::IFramebuffer* Backbuffer = nullptr; // a backbuffer framebuffer: pipelines are built against it
		ShaderLibrary* Shaders = nullptr;          // cooked shaders, loaded through the asset system
		TextureRegistry* Textures = nullptr;       // the textures the UI can draw by id
		GpuResourceCache* Cache = nullptr;           // the meshes, textures and fonts on the GPU
	};

	// A render pass records its commands into the command list that the renderer
	// has already opened. Passes run in the order they were added.
	class IRenderPass
	{
	public:
		virtual ~IRenderPass() = default;

		// Shown in the timings table and used as the debug marker name (RenderDoc, Nsight).
		virtual const char* get_name() const = 0;

		// Creates resources and requests the shaders it needs. Must not wait for them.
		virtual bool initialize(const PassInitializationDescription& description) = 0;

		// Render thread, every frame: creates the pipelines whose shaders have become ready (or changed).
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) {}

		// The renderer only runs the passes once all of them are ready.
		virtual bool is_ready() const { return true; }
		virtual bool has_failed() const { return false; }

		virtual void on_resize(nvrhi::IDevice* device, u32 width, u32 height) {}

		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) = 0;

		// Render thread, after the frame was submitted (only for frames whose passes ran): for work that needs the GPU to have run it.
		virtual void on_frame_submitted(nvrhi::IDevice* device, const FrameDescription& frame) {}
	};
}
