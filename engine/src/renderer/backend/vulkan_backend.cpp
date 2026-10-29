// vulkan.hpp is only needed for NVRHI's dynamic dispatcher: NVRHI's Vulkan
// backend calls through VULKAN_HPP_DEFAULT_DISPATCHER, so the host app has to
// provide the storage and initialize it. If the linker reports a duplicate
// definition of the dispatcher, your NVRHI build already provides it: remove
// the lines marked (dispatcher) below.
#include <pch.h>
#include <vulkan/vulkan.hpp>
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE // (dispatcher)

#include "vulkan_backend.h"

#include <algorithm>
#include <GLFW/glfw3.h>

#include <nvrhi/validation.h>


namespace wiwa
{

	namespace
	{
		// The validation layer's messages go to the log; errors are counted for the render smoke test.
		VKAPI_ATTR VkBool32 VKAPI_CALL vulkan_debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
			VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
		{
			switch (severity)
			{
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
				WI_LOG(Error, "Vulkan", "{0}", data->pMessage);
				report_backend_error();
				break;
			case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
				WI_LOG(Warning, "Vulkan", "{0}", data->pMessage);
				break;
			default:
				WI_LOG(Info, "Vulkan", "{0}", data->pMessage);
				break;
			}
			return VK_FALSE;
		}
	}

	VulkanBackend::~VulkanBackend()
	{
		shutdown();
	}

	bool VulkanBackend::initialize(const RendererDescription& description, u32 width, u32 height)
	{
		m_Description = description;

		return create_vulkan_objects()
			&& create_nvrhi_device()
			&& create_swapchain(width, height)
			&& create_semaphores(m_AcquireSemaphores, std::max(m_Description.MaxFramesInFlight, 1u))
			&& create_swapchain_resources();
	}

	void VulkanBackend::shutdown()
	{
		if (m_VkbDevice.device == VK_NULL_HANDLE && m_VkbInstance.instance == VK_NULL_HANDLE)
		{
			return;
		}

		if (m_Device)
		{
			m_Device->waitForIdle();
			destroy_swapchain_resources();
			m_Device->runGarbageCollection();
		}

		m_Device = nullptr;
		m_VulkanDevice = nullptr;

		if (m_VkbDevice.device != VK_NULL_HANDLE)
		{
			vkDeviceWaitIdle(m_VkbDevice.device);

			destroy_semaphores(m_PresentSemaphores);
			destroy_semaphores(m_AcquireSemaphores);

			if (m_Swapchain.swapchain != VK_NULL_HANDLE)
			{
				vkb::destroy_swapchain(m_Swapchain);
				m_Swapchain = vkb::Swapchain{};
			}

			vkb::destroy_device(m_VkbDevice);
			m_VkbDevice = vkb::Device{};
		}

		if (m_Surface != VK_NULL_HANDLE)
		{
			vkb::destroy_surface(m_VkbInstance, m_Surface);
			m_Surface = VK_NULL_HANDLE;
		}

		if (m_VkbInstance.instance != VK_NULL_HANDLE)
		{
			vkb::destroy_instance(m_VkbInstance);
			m_VkbInstance = vkb::Instance{};
		}
	}

	nvrhi::IFramebuffer* VulkanBackend::get_reference_framebuffer() const
	{
		return m_Framebuffers.empty() ? nullptr : m_Framebuffers[0].Get();
	}

	FrameStatus VulkanBackend::acquire_frame(nvrhi::IFramebuffer*& backbuffer)
	{
		VkSemaphore acquireSemaphore = m_AcquireSemaphores[m_AcquireIndex % m_AcquireSemaphores.size()];

		const VkResult result = vkAcquireNextImageKHR(m_VkbDevice.device, m_Swapchain.swapchain, UINT64_MAX,
			acquireSemaphore, VK_NULL_HANDLE, &m_CurrentImageIndex);

		if (result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			return FrameStatus::ResizeNeeded;
		}
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
		{
			WI_LOG(Error, "Vulkan", "vkAcquireNextImageKHR failed ({0})", result);
			return FrameStatus::Failed;
		}

		// Only successful acquires advance the index: a failed one never signals its semaphore.
		m_AcquireIndex++;

		// The graphics queue must wait for the image to be acquired before it touches it.
		m_VulkanDevice->queueWaitForSemaphore(nvrhi::CommandQueue::Graphics, acquireSemaphore, 0);

		backbuffer = m_Framebuffers[m_CurrentImageIndex];
		return FrameStatus::Ok;
	}

	FrameStatus VulkanBackend::submit_and_present(nvrhi::ICommandList* commandList)
	{
		// Signal when rendering is done so presentation can wait for it.
		m_VulkanDevice->queueSignalSemaphore(nvrhi::CommandQueue::Graphics, m_PresentSemaphores[m_CurrentImageIndex], 0);
		m_Device->executeCommandList(commandList);

		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &m_PresentSemaphores[m_CurrentImageIndex];
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &m_Swapchain.swapchain;
		presentInfo.pImageIndices = &m_CurrentImageIndex;

		const VkResult result = vkQueuePresentKHR(m_PresentQueue, &presentInfo);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
		{
			return FrameStatus::ResizeNeeded;
		}
		if (result != VK_SUCCESS)
		{
			WI_LOG(Error, "Vulkan", "vkQueuePresentKHR failed ({0})", result);
			return FrameStatus::Failed;
		}

		// No waiting here: the renderer limits how far the CPU runs ahead (frames in flight).
		m_Device->runGarbageCollection();

		return FrameStatus::Ok;
	}

	bool VulkanBackend::resize(u32 width, u32 height)
	{
		m_Device->waitForIdle();
		vkDeviceWaitIdle(m_VkbDevice.device);

		destroy_swapchain_resources();
		m_Device->runGarbageCollection();

		return create_swapchain(width, height) && create_swapchain_resources();
	}

	bool VulkanBackend::set_vsync(bool enabled)
	{
		if (m_Description.EnableVsync == enabled)
		{
			return false;
		}

		// The present mode is fixed at swapchain creation.
		m_Description.EnableVsync = enabled;
		return true;
	}

	bool VulkanBackend::create_vulkan_objects()
	{
		vkb::InstanceBuilder instanceBuilder;
		instanceBuilder.set_app_name(m_Description.ApplicationName.c_str());
		instanceBuilder.require_api_version(1, 3, 0);

		if (m_Description.EnableValidation)
		{
			instanceBuilder.request_validation_layers(true);
			instanceBuilder.set_debug_callback(vulkan_debug_callback);
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
		if (!instanceResult)
		{
			WI_LOG(Error, "Vulkan", "failed to create Vulkan instance: {0}", instanceResult.error().message());
			return false;
		}
		m_VkbInstance = instanceResult.value();

		VULKAN_HPP_DEFAULT_DISPATCHER.init(m_VkbInstance.fp_vkGetInstanceProcAddr); // (dispatcher)
		VULKAN_HPP_DEFAULT_DISPATCHER.init(vk::Instance(m_VkbInstance.instance)); // (dispatcher)

		if (glfwCreateWindowSurface(m_VkbInstance.instance, m_Description.Window, nullptr, &m_Surface) != VK_SUCCESS)
		{
			WI_LOG(Error, "Vulkan", "failed to create window surface");
			return false;
		}

		// NVRHI's Vulkan backend relies on these core features; it never enables them itself.
		VkPhysicalDeviceVulkan12Features features12{};
		features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
		features12.timelineSemaphore = VK_TRUE;

		VkPhysicalDeviceVulkan13Features features13{};
		features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
		features13.dynamicRendering = VK_TRUE;
		features13.synchronization2 = VK_TRUE;

		// The G-buffer sampler is anisotropic; the models are drawn with one indirect call per material (several draws each).
		VkPhysicalDeviceFeatures features{};
		features.samplerAnisotropy = VK_TRUE;
		features.multiDrawIndirect = VK_TRUE;
		features.fillModeNonSolid = VK_TRUE; // the wireframe debug view
		features.independentBlend = VK_TRUE; // the overdraw debug view blends into one of the G-buffer targets only

		vkb::PhysicalDeviceSelector selector(m_VkbInstance);
		auto physicalDeviceResult = selector
			.set_surface(m_Surface)
			.set_minimum_version(1, 3)
			.set_required_features(features)
			.set_required_features_12(features12)
			.set_required_features_13(features13)
			.select();
		if (!physicalDeviceResult)
		{
			WI_LOG(Error, "Vulkan", "no suitable GPU: {0}", physicalDeviceResult.error().message());
			return false;
		}

		vkb::DeviceBuilder deviceBuilder(physicalDeviceResult.value());
		auto deviceResult = deviceBuilder.build();
		if (!deviceResult)
		{
			WI_LOG(Error, "Vulkan", "failed to create Vulkan device: {0}", deviceResult.error().message());
			return false;
		}
		m_VkbDevice = deviceResult.value();
		m_DeviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

		VULKAN_HPP_DEFAULT_DISPATCHER.init(vk::Device(m_VkbDevice.device)); // (dispatcher)

		auto graphicsQueue = m_VkbDevice.get_queue(vkb::QueueType::graphics);
		auto graphicsQueueIndex = m_VkbDevice.get_queue_index(vkb::QueueType::graphics);
		auto presentQueue = m_VkbDevice.get_queue(vkb::QueueType::present);
		if (!graphicsQueue || !graphicsQueueIndex || !presentQueue)
		{
			WI_LOG(Error, "Vulkan", "failed to get device queues");
			return false;
		}

		m_GraphicsQueue = graphicsQueue.value();
		m_GraphicsQueueIndex = graphicsQueueIndex.value();
		m_PresentQueue = presentQueue.value();

		return true;
	}

	bool VulkanBackend::create_nvrhi_device()
	{
		auto toPointers = [](const std::vector<std::string>& names)
		{
			std::vector<const char*> pointers;
			pointers.reserve(names.size());
			for (const std::string& name : names)
			{
				pointers.push_back(name.c_str());
			}
			return pointers;
		};

		std::vector<const char*> instanceExtensions = toPointers(m_InstanceExtensions);
		std::vector<const char*> deviceExtensions = toPointers(m_DeviceExtensions);

		nvrhi::vulkan::DeviceDesc deviceDescription;
		deviceDescription.errorCB = get_nvrhi_message_callback();
		deviceDescription.instance = m_VkbInstance.instance;
		deviceDescription.physicalDevice = m_VkbDevice.physical_device.physical_device;
		deviceDescription.device = m_VkbDevice.device;
		deviceDescription.graphicsQueue = m_GraphicsQueue;
		deviceDescription.graphicsQueueIndex = static_cast<int>(m_GraphicsQueueIndex);
		deviceDescription.instanceExtensions = instanceExtensions.data();
		deviceDescription.numInstanceExtensions = instanceExtensions.size();
		deviceDescription.deviceExtensions = deviceExtensions.data();
		deviceDescription.numDeviceExtensions = deviceExtensions.size();

		m_VulkanDevice = nvrhi::vulkan::createDevice(deviceDescription);
		if (!m_VulkanDevice)
		{
			WI_LOG(Error, "Vulkan", "failed to create NVRHI Vulkan device");
			return false;
		}

		m_Device = m_Description.EnableValidation
			? nvrhi::validation::createValidationLayer(m_VulkanDevice)
			: nvrhi::DeviceHandle(m_VulkanDevice);

		return true;
	}

	bool VulkanBackend::create_swapchain(u32 width, u32 height)
	{
		vkb::SwapchainBuilder builder(m_VkbDevice);
		builder.set_desired_format({ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR });
		if (m_Description.EnableVsync)
		{
			builder.set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR);
		}
		else
		{
			// Uncapped: mailbox where available (no tearing), otherwise immediate. FIFO is the last resort, always supported.
			builder.set_desired_present_mode(VK_PRESENT_MODE_MAILBOX_KHR);
			builder.add_fallback_present_mode(VK_PRESENT_MODE_IMMEDIATE_KHR);
			builder.add_fallback_present_mode(VK_PRESENT_MODE_FIFO_KHR);
		}
		builder.set_desired_extent(width, height);
		builder.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_DST_BIT); // needed for clears
		builder.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_SRC_BIT); // needed for screenshots

		if (m_Swapchain.swapchain != VK_NULL_HANDLE)
		{
			builder.set_old_swapchain(m_Swapchain);
		}

		auto swapchainResult = builder.build();
		if (!swapchainResult)
		{
			WI_LOG(Error, "Vulkan", "failed to create swapchain: {0}", swapchainResult.error().message());
			return false;
		}

		vkb::Swapchain oldSwapchain = m_Swapchain;
		m_Swapchain = swapchainResult.value();
		if (oldSwapchain.swapchain != VK_NULL_HANDLE)
		{
			vkb::destroy_swapchain(oldSwapchain);
		}

		switch (m_Swapchain.image_format)
		{
		case VK_FORMAT_B8G8R8A8_UNORM:
			m_SwapchainFormat = nvrhi::Format::BGRA8_UNORM;
			return true;
		case VK_FORMAT_R8G8B8A8_UNORM:
			m_SwapchainFormat = nvrhi::Format::RGBA8_UNORM;
			return true;
		default:
			WI_LOG(Error, "Vulkan", "unsupported swapchain format {0}", m_Swapchain.image_format);
			return false;
		}
	}

	bool VulkanBackend::create_swapchain_resources()
	{
		auto imagesResult = m_Swapchain.get_images();
		if (!imagesResult)
		{
			WI_LOG(Error, "Vulkan", "failed to get swapchain images");
			return false;
		}

		const std::vector<VkImage>& images = imagesResult.value();
		m_SwapchainTextures.reserve(images.size());
		m_Framebuffers.reserve(images.size());

		nvrhi::TextureDesc textureDescription;
		textureDescription.width = m_Swapchain.extent.width;
		textureDescription.height = m_Swapchain.extent.height;
		textureDescription.format = m_SwapchainFormat;
		textureDescription.debugName = "Swapchain image";
		textureDescription.isRenderTarget = true;
		textureDescription.initialState = nvrhi::ResourceStates::Present;
		textureDescription.keepInitialState = true;

		for (VkImage image : images)
		{
			nvrhi::TextureHandle texture = m_Device->createHandleForNativeTexture(
				nvrhi::ObjectTypes::VK_Image, nvrhi::Object(image), textureDescription);
			m_SwapchainTextures.push_back(texture);

			nvrhi::FramebufferDesc framebufferDescription;
			framebufferDescription.addColorAttachment(texture);
			m_Framebuffers.push_back(m_Device->createFramebuffer(framebufferDescription));
		}

		// One "render finished" semaphore per image so a semaphore is never reused
		// while a present operation may still be waiting on it.
		if (m_PresentSemaphores.size() != m_Framebuffers.size())
		{
			destroy_semaphores(m_PresentSemaphores);
			return create_semaphores(m_PresentSemaphores, m_Framebuffers.size());
		}

		return true;
	}

	bool VulkanBackend::create_semaphores(std::vector<VkSemaphore>& semaphores, size_t count)
	{
		VkSemaphoreCreateInfo semaphoreInfo{};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		semaphores.reserve(semaphores.size() + count);
		for (size_t i = 0; i < count; i++)
		{
			VkSemaphore semaphore = VK_NULL_HANDLE;
			if (vkCreateSemaphore(m_VkbDevice.device, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS)
			{
				WI_LOG(Error, "Vulkan", "vkCreateSemaphore failed");
				return false;
			}
			semaphores.push_back(semaphore);
		}
		return true;
	}

	void VulkanBackend::destroy_semaphores(std::vector<VkSemaphore>& semaphores)
	{
		for (VkSemaphore semaphore : semaphores)
		{
			vkDestroySemaphore(m_VkbDevice.device, semaphore, nullptr);
		}
		semaphores.clear();
	}

	void VulkanBackend::destroy_swapchain_resources()
	{
		m_Framebuffers.clear();
		m_SwapchainTextures.clear();
	}
}
