#pragma once
#include "render_pass.h"
#include <nvrhi/nvrhi.h>

namespace wiwa
{
	struct DeviceDescription
	{
		nvrhi::GraphicsAPI Backend;
		Iv2 BackBufferWidth = { 1280, 720 };
		u32 RefreshRate = 0;
		u32 SwapchainBufferCount = 3;
		nvrhi::Format SwapchainFormat = nvrhi::Format::SRGBA8_UNORM;
		u32 SwapchainSampleCount = 1;
		u32 SwapchainSampleQuality = 0;
		u32 MaxFramesInFlight = 3;

		bool EnableValidationLayer = true;
		bool EnableVSync = false;
		bool EnableComputeQueue = false;
		bool EnableCopyQueue = true;
	};

	struct DefaultMessageCallback : public nvrhi::IMessageCallback
	{
		static DefaultMessageCallback& get_instance();
		void message(nvrhi::MessageSeverity severity, const char* messageText) override;
	};

	class IDevice
	{
	public:
		virtual bool initialize() = 0;
		virtual void shutdown() = 0;
		virtual nvrhi::IDevice* get_device() const = 0;
		virtual bool update_swapchain(u32 width, u32 height) = 0;
		virtual void begin_frame(float deltaTime) = 0;
		virtual void end_frame(float deltaTime) = 0;

		virtual nvrhi::FramebufferHandle get_current_framebuffer() const = 0;
	};

	Ref<IDevice> create_device(DeviceDescription desc);
}