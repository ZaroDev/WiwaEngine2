#include <pch.h>
#include "vulkan_device.h"

#include <vulkan/vulkan.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

#include <nvrhi/validation.h>

#include <core/engine.h>
#include <renderer/window.h>

#include <GLFW/glfw3.h>

#define WI_LOG_VULKAN(level, ...) WI_LOG(level, "Vulkan", __VA_ARGS__)
#define RETURN_IF_FAIL(x) if(!x) { WI_LOG_VULKAN(Error, "Failed at {0}", #x); return false; }
#define VK_RETURN_IF_FAIL(x) if(x != VK_SUCCESS) { WI_LOG_VULKAN(Error, "Failed at {0}", #x); return false; }

namespace wiwa
{
	namespace
	{
		const std::vector<const char*> c_VkValidationLayers =
		{
			"VK_LAYER_KHRONOS_validation"
		};

		std::vector<const char*> getRequiredExtensions() 
		{
			uint32_t glfwExtensionCount = 0;
			const char** glfwExtensions;
			glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

			std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
			extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
			

			return extensions;
		}

		static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
			VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
			VkDebugUtilsMessageTypeFlagsEXT messageType,
			const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
			void* pUserData) 
		{
			switch (messageSeverity)
			{
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: WI_LOG_VULKAN(Info, "{}", pCallbackData->pMessage); break;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: WI_LOG_VULKAN(Info, "{}", pCallbackData->pMessage); break;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: WI_LOG_VULKAN(Warning, "{}", pCallbackData->pMessage); break;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: WI_LOG_VULKAN(Error, "{}", pCallbackData->pMessage); break;
			}
			

			return VK_FALSE;
		}
	}

	VulkanDevice::VulkanDevice(const DeviceDescription& desc)
		: m_DeviceDesc(desc)
	{
	}
	bool VulkanDevice::initialize()
	{
		RETURN_IF_FAIL(create_vulkan_objects());
		RETURN_IF_FAIL(create_nvrhi_device());
		RETURN_IF_FAIL(create_swapchain(m_DeviceDesc.BackBufferWidth.x, m_DeviceDesc.BackBufferWidth.y));
		RETURN_IF_FAIL(create_sync_objects());
		RETURN_IF_FAIL(create_swapchain_resources());

		return true;
	}
	void VulkanDevice::shutdown()
	{
		if (m_VkDevice.device == VK_NULL_HANDLE && m_VkInstance.instance == VK_NULL_HANDLE)
		{
			return;
		}

		if (m_DeviceHandle)
		{
			m_DeviceHandle->waitForIdle();
		}

		
		
		destroy_swapchain_resources();

		if (m_DeviceHandle)
		{
			m_DeviceHandle->runGarbageCollection();
		}

		m_DeviceHandle = nullptr;
		m_ValidationLayer = nullptr;

		if (m_VkDevice.device != VK_NULL_HANDLE)
		{
			vkDeviceWaitIdle(m_VkDevice.device);

			destroy_present_semaphores();
			for (auto& semaphore : m_AdquireSemaphores)
			{
				vkDestroySemaphore(m_VkDevice.device, semaphore, nullptr);
			}
			m_AdquireSemaphores.clear();

			if (m_VkSwapchain.swapchain != VK_NULL_HANDLE)
			{
				vkb::destroy_swapchain(m_VkSwapchain);
				m_VkSwapchain = vkb::Swapchain{};
			}

			vkb::destroy_device(m_VkDevice);
			m_VkDevice = vkb::Device{};
		}

		if (m_Surface != VK_NULL_HANDLE)
		{
			vkb::destroy_surface(m_VkInstance, m_Surface);
			m_Surface = VK_NULL_HANDLE;
		}

		if (m_VkInstance.instance != VK_NULL_HANDLE)
		{
			vkb::destroy_instance(m_VkInstance);
			m_VkInstance = vkb::Instance{};
		}
	}
	bool VulkanDevice::update_swapchain(u32 width, u32 height)
	{
		m_DeviceHandle->waitForIdle();
		vkDeviceWaitIdle(m_VkDevice.device);

		destroy_swapchain_resources();
		m_DeviceHandle->runGarbageCollection();

		RETURN_IF_FAIL(create_swapchain(width, height));
		RETURN_IF_FAIL(create_swapchain_resources());
	
		return true;
	}
	nvrhi::FramebufferHandle VulkanDevice::get_current_framebuffer() const
	{
		return m_Framebuffers.empty() ? nullptr : static_cast<nvrhi::IFramebuffer*>(m_Framebuffers[m_CurrentImageIndex]);
	}
	FrameStatus VulkanDevice::submit_and_present(nvrhi::ICommandList* commandList)
	{
		m_DeviceHandle->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, m_PresentSemaphores[m_CurrentImageIndex], 0);
		m_DeviceHandle->executeCommandList(commandList);

		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &m_PresentSemaphores[m_CurrentImageIndex];
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &m_VkSwapchain.swapchain;
		presentInfo.pImageIndices = &m_CurrentImageIndex;

		VkResult result = vkQueuePresentKHR(m_PresentQueue, &presentInfo);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
		{
			return FrameStatus::RESIZE_NEEDED;
		}
		if (result != VK_SUCCESS)
		{
			WI_LOG_VULKAN(Error, "vkQueuePresentKHR failed ({0})", result);
			return FrameStatus::FAILED;
		}

		// No waiting here: the renderer limits how far the CPU runs ahead (frames in flight).
		m_DeviceHandle->runGarbageCollection();

		return FrameStatus::OK;
	}
	FrameStatus VulkanDevice::adquire_frame(nvrhi::IFramebuffer*& backbuffer)
	{
		VkSemaphore adquireSemaphore = m_AdquireSemaphores[m_AdquireIndex % m_AdquireSemaphores.size()];
		VkResult result = vkAcquireNextImageKHR(m_VkDevice.device, m_VkSwapchain.swapchain, UINT64_MAX, adquireSemaphore, VK_NULL_HANDLE, &m_CurrentImageIndex);

		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			return FrameStatus::RESIZE_NEEDED;
		}
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
		{
			WI_LOG_VULKAN(Error, "vkAdquireNextImageKHR failed ({0})", result);
			return FrameStatus::FAILED;
		}

		m_AdquireIndex++;
		m_DeviceHandle->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, adquireSemaphore, 0);

		backbuffer = m_Framebuffers[m_CurrentImageIndex];
		return FrameStatus::OK;
	}
	
	bool VulkanDevice::create_vulkan_objects()
	{
		vkb::InstanceBuilder instanceBuilder;
		instanceBuilder.set_app_name("Wiwa2App");
		instanceBuilder.require_api_version(1, 3, 0);

		if (m_DeviceDesc.EnableValidationLayer)
		{
			instanceBuilder.request_validation_layers(true);
			instanceBuilder.set_debug_callback(debugCallback);
			m_InstanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}

		u32 glfwExtensionCount = 0;
		const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

		for (u32 i = 0; i < glfwExtensionCount; i++)
		{
			instanceBuilder.enable_extension(glfwExtensions[i]);
			m_InstanceExtensions.push_back(glfwExtensions[i]);
		}


		auto instanceResult = instanceBuilder.build();
		RETURN_IF_FAIL(instanceResult);

		m_VkInstance = instanceResult.value();

		VULKAN_HPP_DEFAULT_DISPATCHER.init(m_VkInstance.fp_vkGetInstanceProcAddr);
		VULKAN_HPP_DEFAULT_DISPATCHER.init(vk::Instance(m_VkInstance.instance));

		GLFWwindow* window = Engine::get().get_module<Window>()->get_window();

		VK_RETURN_IF_FAIL(glfwCreateWindowSurface(m_VkInstance.instance, window, nullptr, &m_Surface));

		VkPhysicalDeviceVulkan12Features features12{};
		features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		features12.timelineSemaphore = VK_TRUE;

		VkPhysicalDeviceVulkan13Features features13{};
		features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		features13.dynamicRendering = VK_TRUE;
		features13.synchronization2 = VK_TRUE;

		vkb::PhysicalDeviceSelector selector(m_VkInstance);
		auto physicalDeviceResult = selector
			.set_surface(m_Surface)
			.set_minimum_version(1, 3)
			.set_required_features_12(features12)
			.set_required_features_13(features13)
			.select();

		RETURN_IF_FAIL(physicalDeviceResult);

		vkb::DeviceBuilder deviceBuilder(physicalDeviceResult.value());
		auto deviceResult = deviceBuilder.build();
		RETURN_IF_FAIL(deviceResult);

		m_VkDevice = deviceResult.value();
		m_DeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

		VULKAN_HPP_DEFAULT_DISPATCHER.init(vk::Device(m_VkDevice.device));


		auto graphicsQueue = m_VkDevice.get_queue(vkb::QueueType::graphics);
		auto graphicsQueueIndex = m_VkDevice.get_queue_index(vkb::QueueType::graphics);
		auto presentQueue = m_VkDevice.get_queue(vkb::QueueType::present);

		RETURN_IF_FAIL(graphicsQueue);
		RETURN_IF_FAIL(graphicsQueueIndex);
		RETURN_IF_FAIL(presentQueue);

		m_GraphicsQueue = graphicsQueue.value();
		m_GraphicsQueueIndex = graphicsQueueIndex.value();
		m_PresentQueue = presentQueue.value();

		return true;
	}
	bool VulkanDevice::create_nvrhi_device()
	{
		nvrhi::vulkan::DeviceDesc deviceDesc{};
		deviceDesc.errorCB = &DefaultMessageCallback::get_instance();
		deviceDesc.instance = m_VkInstance.instance;
		deviceDesc.physicalDevice = m_VkDevice.physical_device.physical_device;
		deviceDesc.device = m_VkDevice.device;
		deviceDesc.graphicsQueue = m_GraphicsQueue;
		deviceDesc.graphicsQueueIndex = static_cast<int>(m_GraphicsQueueIndex);
		deviceDesc.instanceExtensions = m_InstanceExtensions.data();
		deviceDesc.numInstanceExtensions = m_InstanceExtensions.size();
		deviceDesc.deviceExtensions = m_DeviceExtensions.data();
		deviceDesc.numDeviceExtensions = m_DeviceExtensions.size();

		m_DeviceHandle = nvrhi::vulkan::createDevice(deviceDesc);

		RETURN_IF_FAIL(m_DeviceHandle);

		if (m_DeviceDesc.EnableValidationLayer)
		{
			m_ValidationLayer = nvrhi::validation::createValidationLayer(m_DeviceHandle);
		}
	}
	bool VulkanDevice::create_swapchain(u32 width, u32 height)
	{
		vkb::SwapchainBuilder builder(m_VkDevice);
		builder.set_desired_format({ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR });
		builder.set_desired_present_mode(m_DeviceDesc.EnableVSync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR);
		builder.set_desired_extent(width, height);
		builder.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT); // needed for clears

		if (m_VkSwapchain.swapchain != VK_NULL_HANDLE)
		{
			builder.set_old_swapchain(m_VkSwapchain);
		}

		auto swapchainResult = builder.build();
		RETURN_IF_FAIL(swapchainResult);

		vkb::Swapchain oldSwapchain = m_VkSwapchain;
		m_VkSwapchain = swapchainResult.value();

		if (oldSwapchain.swapchain != VK_NULL_HANDLE)
		{
			vkb::destroy_swapchain(oldSwapchain);
		}

		switch (m_VkSwapchain.image_format)	
		{
		case VK_FORMAT_B8G8R8A8_UNORM:
			m_SwapchainFormat = nvrhi::Format::BGRA8_UNORM;
			break;
		case VK_FORMAT_R8G8B8A8_UNORM:
			m_SwapchainFormat = nvrhi::Format::RGBA8_UNORM;
			break;
		default:
			WI_LOG_VULKAN(Error, "Unsuported swapchain format {0}", m_VkSwapchain.image_format);
			return false;
		}

		return true;
	}
	bool VulkanDevice::create_swapchain_resources()
	{
		auto imagesResult = m_VkSwapchain.get_images();
		RETURN_IF_FAIL(imagesResult);

		for (VkImage image : imagesResult.value())
		{
			nvrhi::TextureDesc textureDescription;
			textureDescription.width = m_VkSwapchain.extent.width;
			textureDescription.height = m_VkSwapchain.extent.height;
			textureDescription.format = m_SwapchainFormat;
			textureDescription.debugName = "Swapchain image";
			textureDescription.isRenderTarget = true;
			textureDescription.initialState = nvrhi::ResourceStates::Present;
			textureDescription.keepInitialState = true;

			nvrhi::TextureHandle texture = m_DeviceHandle->createHandleForNativeTexture(
				nvrhi::ObjectTypes::VK_Image, nvrhi::Object(image), textureDescription);
			m_SwapchainTextures.push_back(texture);

			nvrhi::FramebufferDesc framebuffer_description;
			framebuffer_description.addColorAttachment(texture);
			m_Framebuffers.push_back(m_DeviceHandle->createFramebuffer(framebuffer_description));
		}

		// One "render finished" semaphore per image so a semaphore is never reused
		// while a present operation may still be waiting on it.
		if (m_PresentSemaphores.size() != m_Framebuffers.size())
		{
			destroy_present_semaphores();

			VkSemaphoreCreateInfo semaphore_info{};
			semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
			for (size_t i = 0; i < m_Framebuffers.size(); i++)
			{
				VkSemaphore semaphore = VK_NULL_HANDLE;
				if (vkCreateSemaphore(m_VkDevice.device, &semaphore_info, nullptr, &semaphore) != VK_SUCCESS)
				{
					return false;
				}
				m_PresentSemaphores.push_back(semaphore);
			}
		}

		return true;
	}

	bool VulkanDevice::create_sync_objects()
	{
		VkSemaphoreCreateInfo semaphore_info{};
		semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		const uint32_t count = m_DeviceDesc.FramesInFlight > 0 ? m_DeviceDesc.FramesInFlight : 1;
		for (uint32_t i = 0; i < count; i++)
		{
			VkSemaphore semaphore = VK_NULL_HANDLE;
			if (vkCreateSemaphore(m_VkDevice.device, &semaphore_info, nullptr, &semaphore) != VK_SUCCESS)
			{
				return false;
			}
			m_AdquireSemaphores.push_back(semaphore);
		}
		return true;
	}

	void VulkanDevice::destroy_swapchain_resources()
	{
		m_Framebuffers.clear();
		m_SwapchainTextures.clear();
	}

	void VulkanDevice::destroy_present_semaphores()
	{
		for (VkSemaphore semaphore : m_PresentSemaphores)
		{
			vkDestroySemaphore(m_VkDevice.device, semaphore, nullptr);
		}
		m_PresentSemaphores.clear();
	}
}