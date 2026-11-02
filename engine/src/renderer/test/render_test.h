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
	// The scene to render-thread checks (scene_checks.cpp).
	void register_scene_checks();
	// The GPU resource cache checks (cache_checks.cpp).
	void register_cache_checks();
	// The viewport target, texture registry and UI pass checks (viewport_checks.cpp).
	void register_viewport_checks();
	// The camera and editor camera checks (camera_checks.cpp).
	void register_camera_checks();
	// The depth prepass and forward pass checks (forward_checks.cpp).
	void register_forward_checks();
	// The clustered light checks (light_checks.cpp).
	void register_light_checks();
	// The shadow checks (shadow_checks.cpp).
	void register_shadow_checks();
	// The alpha mode, blending, resolve and blit checks (blend_checks.cpp).
	void register_blend_checks();
	// Scale, cross-backend, hot reload, resize, minimize and queue checks (scale_checks.cpp).
	void register_scale_checks();
	// The IBL math checks (ibl_checks.cpp).
	void register_ibl_checks();
	// The scene and quality settings checks (settings_checks.cpp).
	void register_settings_checks();
	// The environment cache checks (environment_checks.cpp).
	void register_environment_checks();
	// The sky pass checks (sky_checks.cpp).
	void register_sky_checks();
	// The atmosphere checks (atmosphere_checks.cpp).
	void register_atmosphere_checks();
	// Tone mapping, exposure, bloom, FXAA and ambient occlusion (post_checks.cpp).
	void register_post_checks();
	// Whole scenes with an environment (environment_scene_checks.cpp).
	void register_environment_scene_checks();
	// The DebugDraw collector (debug_draw_checks.cpp).
	void register_debug_draw_checks();
	// The viewport depth's stencil plane (stencil_checks.cpp).
	void register_stencil_checks();
	// The debug draw pass (debug_pass_checks.cpp).
	void register_debug_pass_checks();
	// The selection outline (outline_checks.cpp).
	void register_outline_checks();
	// GPU picking (pick_checks.cpp).
	void register_pick_checks();
	// The temporal chain: TAA maths, velocity, TAA, motion blur (temporal_checks.cpp).
	void register_temporal_checks();
	// Custom materials: surface shader cache, custom records, parameter bytes (material_checks.cpp).
	void register_material_checks();

	// `--render-test`: runs every check once on the render thread. True when all passed.
	bool run_render_checks(RenderWorld& world);

	// Copies a mip of an RGBA8 texture to the CPU (a staging texture and a wait for the GPU): width * height * 4 bytes.
	// One array slice and mip of a texture of any format as raw bytes (bytesPerPixel of them per texel, rows packed).
	bool read_texture_slice_bytes(nvrhi::IDevice* device, nvrhi::ITexture* texture, u32 arraySlice, u32 mip, std::vector<u8>& bytes, u32& width, u32& height, u32 bytesPerPixel);
	bool read_texture_rgba8(nvrhi::IDevice* device, nvrhi::ITexture* texture, std::vector<u8>& pixels, u32& width, u32& height, u32 mip = 0);
	// Copies a range of a buffer to the CPU.
	bool read_buffer_bytes(nvrhi::IDevice* device, nvrhi::IBuffer* buffer, u64 byteCount, std::vector<u8>& bytes);

	// Uploads a 64 KiB buffer and a 128x128 RGBA8 texture with 3 mips with a known pattern through a command list on the copy
	// queue (the graphics queue when useCopyQueue is false), has the graphics queue wait for it, reads both back and compares.
	bool copy_queue_roundtrip(nvrhi::IDevice* device, bool useCopyQueue, std::string& error);
}
