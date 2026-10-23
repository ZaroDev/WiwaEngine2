#pragma once
#include "render_pass.h"
#include <nvrhi/nvrhi.h>

namespace wiwa
{
	struct DeviceDescription
	{
		Iv2 BackBufferWidth = { 1280, 720 };
		u32 RefreshRate = 0;
		u32 FramesInFlight = 3;

		bool EnableValidationLayer = true;
		bool EnableVSync = false;
		bool EnableComputeQueue = false;
		bool EnableCopyQueue = true;

		nvrhi::Format SwapchainFormat = nvrhi::Format::SRGBA8_UNORM;
		nvrhi::GraphicsAPI Backend = nvrhi::GraphicsAPI::VULKAN;
	};



	struct DefaultMessageCallback : public nvrhi::IMessageCallback
	{
		static DefaultMessageCallback& get_instance();
		void message(nvrhi::MessageSeverity severity, const char* messageText) override;
	};

	enum class FrameStatus
	{
		OK,
		RESIZE_NEEDED,
		FAILED
	};

	class IDevice
	{
	public:
		virtual ~IDevice() = default;

		virtual bool initialize() = 0;
		virtual void shutdown() = 0;

		virtual nvrhi::IDevice* get_device() const = 0;
		virtual const DeviceDescription& get_description() const = 0;

		virtual bool update_swapchain(u32 width, u32 height) = 0;
		virtual u32 get_width() const = 0;
		virtual u32 get_height() const = 0;

		virtual nvrhi::FramebufferHandle get_current_framebuffer() const = 0;

		virtual FrameStatus submit_and_present(nvrhi::ICommandList* commandList) = 0;
		virtual FrameStatus adquire_frame(nvrhi::IFramebuffer*& backbuffer) = 0;
	};

	Ref<IDevice> create_device(DeviceDescription desc);
}