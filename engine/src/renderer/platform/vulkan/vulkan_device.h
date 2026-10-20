#pragma once

#include <renderer/device.h>

#include <vulkan/vulkan.h>

#include "VkBootstrap.h"

#include <nvrhi/vulkan.h>

namespace wiwa
{
	class VulkanDevice final : public IDevice
	{
	public:
		VulkanDevice(const DeviceDescription& desc);

		virtual ~VulkanDevice() = default;

		virtual bool initialize() override;
		virtual void shutdown() override;
		virtual nvrhi::IDevice* get_device() const override { return m_DeviceHandle; }
		virtual bool update_swapchain(u32 width, u32 height) override;

		virtual void begin_frame(float deltaTime) override;
		virtual void end_frame(float deltaTime) override;

		virtual nvrhi::FramebufferHandle get_current_framebuffer() const override { return m_Framebuffers[m_CurrentImageIndex]; }

	private:
		bool create_vulkan_objects();
		bool create_nvrhi_device();
		bool create_swapchain(u32 width, u32 height);
		bool create_swapchain_resources();
		bool create_sync_objects();
		void destroy_swapchain_resources();
		void destroy_present_semaphores();

	private:
		DeviceDescription m_DeviceDesc;

		vkb::Instance m_VkInstance{};
		vkb::Device m_VkDevice{};
		vkb::Swapchain m_VkSwapchain{};
		VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
		VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
		VkQueue m_PresentQueue = VK_NULL_HANDLE;
		u32 m_GraphicsQueueIndex = 0;

		nvrhi::vulkan::DeviceHandle m_DeviceHandle;
		nvrhi::DeviceHandle m_ValidationLayer;
		nvrhi::Format m_SwapchainFormat = nvrhi::Format::SRGBA8_UNORM;

		std::vector<nvrhi::TextureHandle> m_SwapchainTextures;
		std::vector<nvrhi::FramebufferHandle> m_Framebuffers;

		VkSemaphore m_AdquireSemaphore = VK_NULL_HANDLE;
		std::vector<VkSemaphore> m_PresentSemaphores;

		std::vector<const char*> m_InstanceExtensions;
		std::vector<const char*> m_DeviceExtensions;

		u32 m_CurrentImageIndex = 0;
	};
}