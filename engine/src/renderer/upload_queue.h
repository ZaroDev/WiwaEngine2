#pragma once

#include <chrono>

#include <nvrhi/nvrhi.h>

#include <core/common.h>

namespace wiwa
{
	// Where GPU uploads are recorded: a command list on the copy queue when the device has one (so copies overlap with drawing),
	// else on the graphics queue ahead of the frame's own list. The caller cannot tell which; is_using_copy_queue() says.
	// Render thread only.
	//
	// A frame goes: begin_frame(), any number of write_*() (use has_room() to stay inside the per-frame byte budget), submit()
	// before the frame's graphics list is executed. Whatever was written in a frame is usable by that frame's graphics list.
	//
	// The resources written here must be created with initialState = resting_state(device) and keepInitialState = true: the
	// lists run on different queues, so NVRHI needs a known state to return them to (see resting_state).
	class UploadQueue
	{
	public:
		// copyQueueAvailable: IRenderBackend::has_copy_queue(). bytesPerFrameBudget: see has_room().
		bool initialize(nvrhi::IDevice* device, bool copyQueueAvailable, size_t bytesPerFrameBudget);
		void shutdown();

		// The state an uploaded resource rests in between lists. D3D12 copy queues work in COMMON (a copy promotes the resource
		// implicitly, and the vendored NVRHI records no barriers on copy lists); NVRHI's Vulkan backend has no layout for COMMON
		// and rests in CopyDest. Textures that are written once should be given setPermanentTextureState(ShaderResource) after
		// their first use on the graphics queue, so they stop being transitioned back every frame.
		static nvrhi::ResourceStates resting_state(nvrhi::IDevice* device);

		void begin_frame();
		// True while the budget allows this much more; an upload larger than the whole budget is allowed when nothing was written
		// yet this frame, so a big asset still gets through.
		bool has_room(size_t bytes) const;

		void write_buffer(nvrhi::IBuffer* buffer, const void* data, size_t bytes, u64 offset = 0);
		void write_texture(nvrhi::ITexture* texture, u32 arraySlice, u32 mip, const void* data, size_t rowPitch);

		// Closes the list and executes it (on the copy queue, after which the graphics queue waits for it). Nothing happens when
		// nothing was written.
		// graphicsInstance: the graphics frame submitted last (IRenderBackend::get_last_graphics_instance()). The copy queue waits for it
		// before it writes, because it may write buffers that frame is still reading (material records, mesh ranges).
		void submit(u64 graphicsInstance = 0);

		bool is_using_copy_queue() const { return m_List != nullptr && m_UsingCopyQueue; }
		size_t bytes_this_frame() const { return m_BytesThisFrame; }
		u64 bytes_total() const { return m_BytesTotal; }
		double last_submit_ms() const { return m_LastSubmitMs; }

		// Tests and --no-copy-queue: from now on, record on the graphics queue (or back on the copy queue when it exists).
		void force_graphics_queue(bool forced);

	private:
		bool ensure_open();

		nvrhi::IDevice* m_Device = nullptr;
		nvrhi::CommandListHandle m_CopyList{};
		nvrhi::CommandListHandle m_GraphicsList{};
		nvrhi::ICommandList* m_List = nullptr; // the one in use
		bool m_UsingCopyQueue = false;
		bool m_Open = false;
		size_t m_Budget = 0;
		size_t m_BytesThisFrame = 0;
		u64 m_BytesTotal = 0;
		double m_LastSubmitMs = 0.0;
		std::chrono::steady_clock::time_point m_OpenedAt{};
	};
}
