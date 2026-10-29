#pragma once

#include <nvrhi/nvrhi.h>

#include <math/math.h>

#include "imgui_draw_data.h"

namespace wiwa
{
	class ShaderLibrary;

	// Everything a pass needs to know about the current frame.
	struct FrameDescription
	{
		nvrhi::IFramebuffer* Backbuffer = nullptr;
		u32 Width = 0;
		u32 Height = 0;
		float Time = 0.0f;
		u64 FrameIndex = 0; // counts rendered frames
		const OwnedDrawData* ImGui = nullptr; // the UI of this frame, built on the main thread
	};

	struct PassInitializationDescription
	{
		nvrhi::IDevice* Device = nullptr;
		nvrhi::IFramebuffer* Backbuffer = nullptr; // a backbuffer framebuffer: pipelines are built against it
		ShaderLibrary* Shaders = nullptr;          // cooked shaders, loaded through the asset system
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
	};
}
