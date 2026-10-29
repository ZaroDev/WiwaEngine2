#pragma once

#include <functional>
#include <string>
#include <vector>

#include <nvrhi/nvrhi.h>

#include <core/common.h>

#include "../backend/render_backend.h"

namespace wiwa
{
	class RenderWorld;

	// What a render check gets: the render-side world, the device and a way to report. Checks run on the render thread.
	struct RenderCheckContext
	{
		explicit RenderCheckContext(RenderWorld& world) : World(world) {}

		RenderWorld& World;
		nvrhi::IDevice* Device = nullptr;
		GraphicsApi Api = GraphicsApi::Vulkan;
		bool HasCopyQueue = false;
		u32 Failures = 0;
		std::string CurrentCheck{};

		// Logs the failure (with the check's name) when condition is false.
		void check(bool condition, const std::string& what);
	};

	using RenderCheck = std::function<void(RenderCheckContext&)>;

	void register_render_check(std::string name, RenderCheck check);

	// `--render-test`: runs every check once on the render thread. True when all passed.
	bool run_render_checks(RenderWorld& world);

	// Copies a mip of an RGBA8 texture to the CPU (a staging texture and a wait for the GPU): width * height * 4 bytes.
	bool read_texture_rgba8(nvrhi::IDevice* device, nvrhi::ITexture* texture, std::vector<u8>& pixels, u32& width, u32& height, u32 mip = 0);
	// Copies a range of a buffer to the CPU.
	bool read_buffer_bytes(nvrhi::IDevice* device, nvrhi::IBuffer* buffer, u64 byteCount, std::vector<u8>& bytes);

	// Uploads a 64 KiB buffer and a 128x128 RGBA8 texture with 3 mips with a known pattern through a command list on the copy
	// queue (the graphics queue when useCopyQueue is false), has the graphics queue wait for it, reads both back and compares.
	bool copy_queue_roundtrip(nvrhi::IDevice* device, bool useCopyQueue, std::string& error);
}
