#pragma once

#include <deque>
#include <vector>

#include <nvrhi/nvrhi.h>

#include <core/common.h>

#include "backend/render_backend.h"
#include "pass_profiler.h"
#include "render_pass.h"
#include "render_queue.h"

namespace wiwa
{
	// The render thread's renderer: owns the backend and the passes, and turns a batch into a presented frame.
	// Everything here runs on the render thread.
	class Renderer
	{
	public:
		Renderer() = default;
		~Renderer();

		Renderer(const Renderer&) = delete;
		Renderer& operator=(const Renderer&) = delete;

		bool initialize(const RendererDescription& description);
		void shutdown();

		bool add_pass(Scope<IRenderPass> pass);

		// Draws one frame for the batch (the batch's commands were applied already). Returns false when the renderer has
		// failed and cannot go on (device lost, a pass failed, the swapchain cannot be rebuilt).
		bool render_frame(const RenderBatch& batch);

		nvrhi::IDevice* get_device() const { return m_Backend ? m_Backend->get_device() : nullptr; }
		GraphicsApi get_api() const { return m_Description.Api; }
		std::string get_adapter_name() const { return m_Backend ? m_Backend->get_adapter_name() : std::string{}; }
		u32 get_width() const { return m_Backend ? m_Backend->get_width() : 0; }
		u32 get_height() const { return m_Backend ? m_Backend->get_height() : 0; }

		// Takes effect at the start of the next frame.
		void set_vsync(bool enabled) { m_RequestedVsync = enabled; }
		bool is_vsync_enabled() const { return m_RequestedVsync; }

		// True once every pass can render (until then only the background colour is shown).
		bool are_passes_ready() const { return m_PassesReady; }

		// Averaged per-pass timings, in pass order. Empty until the passes have rendered once.
		const std::vector<PassTiming>& get_pass_timings() const { return m_Profiler.get_timings(); }
		bool are_gpu_timings_available() const { return m_Profiler.are_gpu_timings_available(); }

	private:
		bool apply_vsync(u32 width, u32 height);
		bool resize_backend(u32 width, u32 height);
		PassInitializationDescription make_pass_description() const;
		FrameDescription make_frame_description(nvrhi::IFramebuffer* backbuffer, const RenderBatch& batch);

		// Frames in flight: block until fewer than MaxFramesInFlight frames are still running on the GPU.
		void wait_for_frame_slot();
		// Marks the point the GPU has to reach for the frame just submitted to count as finished.
		void mark_frame_submitted();

	private:
		RendererDescription m_Description{};
		u64 m_FrameIndex = 0;
		float m_Time = 0.0f;
		bool m_RequestedVsync = true;
		u32 m_PresentResizeStreak = 0; // consecutive frames whose present asked for a resize at an unchanged size

		Scope<IRenderBackend> m_Backend{};
		nvrhi::CommandListHandle m_CommandList{};
		std::vector<Scope<IRenderPass>> m_Passes{};
		PassProfiler m_Profiler{};

		// One event query per submitted frame that has not been waited on yet, oldest first.
		std::deque<nvrhi::EventQueryHandle> m_FramesInFlight{};
		std::vector<nvrhi::EventQueryHandle> m_QueryPool{}; // queries ready for reuse

		bool m_PassesReady = false;
		bool m_Initialized = false;
	};
}
