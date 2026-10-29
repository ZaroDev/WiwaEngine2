#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <nvrhi/nvrhi.h>

#include <core/common.h>
#include <math/math.h>

struct GLFWwindow;

namespace wiwa
{
	enum class GraphicsApi
	{
		Vulkan,
		D3D11,
		D3D12,
	};

	// "vulkan", "d3d11", "d3d12"
	std::string_view graphics_api_name(GraphicsApi api);
	// Case-insensitive; nullopt for anything else.
	std::optional<GraphicsApi> parse_graphics_api(std::string_view name);

	struct RendererDescription
	{
		GLFWwindow* Window = nullptr;
		u32 Width = 1280;  // the framebuffer size at start; read on the main thread, GLFW's window queries are main-thread only
		u32 Height = 720;
		std::string ApplicationName = "Wiwa";
		bool EnableValidation = false;
		bool EnableVsync = false;
		// How many frames the CPU may record ahead of the GPU. 1 = no overlap (the CPU
		// waits for each frame to finish), 2 is the usual choice, clamped to 1..8.
		u32 MaxFramesInFlight = 3;
		GraphicsApi Api = GraphicsApi::Vulkan;
	};

	enum class FrameStatus
	{
		Ok,
		ResizeNeeded, // the swapchain no longer matches the window: call resize() and skip this frame
		Failed,
	};

	// Owns everything API specific: the native device, the swapchain and the NVRHI
	// device built on top of them. Everything above it (passes, renderer) only
	// sees NVRHI objects.
	class IRenderBackend
	{
	public:
		virtual ~IRenderBackend() = default;

		virtual bool initialize(const RendererDescription& description, u32 width, u32 height) = 0;
		virtual void shutdown() = 0;

		virtual nvrhi::IDevice* get_device() const = 0;
		virtual u32 get_width() const = 0;
		virtual u32 get_height() const = 0;
		virtual std::string get_adapter_name() const = 0;

		// Any backbuffer framebuffer: pipelines are created against it, and all of them share a layout.
		virtual nvrhi::IFramebuffer* get_reference_framebuffer() const = 0;

		// Picks the backbuffer to render into this frame.
		virtual FrameStatus acquire_frame(nvrhi::IFramebuffer*& backbuffer) = 0;
		// Executes the (closed) command list and presents the backbuffer.
		virtual FrameStatus submit_and_present(nvrhi::ICommandList* commandList) = 0;

		virtual bool resize(u32 width, u32 height) = 0;

		// Changes vertical sync. Returns true when the swapchain has to be recreated (resize() at the
		// current size) for it to take effect; false when it applies from the next present.
		virtual bool set_vsync(bool enabled) = 0;
	};

	// Returns nullptr (after logging why) if the backend cannot be created.
	Scope<IRenderBackend> create_render_backend(GraphicsApi api);

	// Receives NVRHI's messages and writes them to the log.
	nvrhi::IMessageCallback* get_nvrhi_message_callback();

	// How many error-level messages the GPU layers have reported so far (NVRHI, Vulkan validation, the D3D debug layers).
	// The render smoke test fails when this is not zero.
	u32 get_error_message_count();
	// For the backends: count one error message.
	void report_backend_error();
}
