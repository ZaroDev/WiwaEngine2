#pragma once

#include <string>
#include <vector>

#include <vulkan/vulkan.h>
#include <VkBootstrap.h>

#include <nvrhi/vulkan.h>

#include "render_backend.h"


namespace wiwa
{
	class VulkanBackend : public IRenderBackend
	{
	public:
		~VulkanBackend() override;

		std::string get_adapter_name() const override { return m_VkbDevice.physical_device.name; }
		bool has_copy_queue() const override { return m_TransferQueue != VK_NULL_HANDLE; }

		bool initialize(const RendererDescription& description, u32 width, u32 height) override;
		void shutdown() override;

		nvrhi::IDevice* get_device() const override { return m_Device; }
		u32 get_width() const override { return m_Swapchain.extent.width; }
		u32 get_height() const override { return m_Swapchain.extent.height; }
		nvrhi::IFramebuffer* get_reference_framebuffer() const override;

		FrameStatus acquire_frame(nvrhi::IFramebuffer*& backbuffer) override;
		FrameStatus submit_and_present(nvrhi::ICommandList* commandList) override;
		bool resize(u32 width, u32 height) override;
		bool set_vsync(bool enabled) override;

	private:
		bool create_vulkan_objects();
		bool create_nvrhi_device();
		bool create_swapchain(u32 width, u32 height);
		bool create_swapchain_resources();
		bool create_semaphores(std::vector<VkSemaphore>& semaphores, size_t count);
		void destroy_semaphores(std::vector<VkSemaphore>& semaphores);
		void destroy_swapchain_resources();

	private:
		RendererDescription m_Description{};

		// Vulkan objects (owned by us: NVRHI never creates the GAPI device)
		vkb::Instance m_VkbInstance{};
		vkb::Device m_VkbDevice{};
		vkb::Swapchain m_Swapchain{};
		VkSurfaceKHR m_Surface{ VK_NULL_HANDLE };
		VkQueue m_GraphicsQueue{ VK_NULL_HANDLE };
		VkQueue m_PresentQueue{ VK_NULL_HANDLE };
		u32 m_GraphicsQueueIndex{};
		VkQueue m_TransferQueue{ VK_NULL_HANDLE }; // a dedicated transfer family, when the GPU has one and the copy queue is on
		u32 m_TransferQueueIndex{};

		std::vector<std::string> m_InstanceExtensions{};
		std::vector<std::string> m_DeviceExtensions{};

		// NVRHI objects
		nvrhi::vulkan::DeviceHandle m_VulkanDevice{};
		nvrhi::DeviceHandle m_Device{}; // validation layer if enabled, otherwise the Vulkan device
		nvrhi::Format m_SwapchainFormat{ nvrhi::Format::BGRA8_UNORM };
		std::vector<nvrhi::TextureHandle> m_SwapchainTextures{};
		std::vector<nvrhi::FramebufferHandle> m_Framebuffers{};

		// Synchronization
		// One acquire semaphore per frame in flight. The renderer's throttling guarantees the
		// frame that last waited on a semaphore has finished before the semaphore is reused.
		std::vector<VkSemaphore> m_AcquireSemaphores{};
		u32 m_AcquireIndex{}; // counts successful acquires
		std::vector<VkSemaphore> m_PresentSemaphores{}; // one per swapchain image
		u32 m_CurrentImageIndex{};
	};
}
