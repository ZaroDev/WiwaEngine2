#include <pch.h>
#include "device.h"

#include "platform/vulkan/vulkan_device.h"

#define WI_LOG_NVRHI(level, ...) WI_LOG(level, "NVRHI", __VA_ARGS__)

namespace wiwa
{
	DefaultMessageCallback& DefaultMessageCallback::get_instance()
	{
		static DefaultMessageCallback instance;
		return instance;
	}
	void DefaultMessageCallback::message(nvrhi::MessageSeverity severity, const char* messageText)
	{
		switch (severity)
		{
		case nvrhi::MessageSeverity::Info: WI_LOG_NVRHI(Info, "{0}", messageText);
			break;
		case nvrhi::MessageSeverity::Warning: WI_LOG_NVRHI(Warning, "{0}", messageText);
			break;
		case nvrhi::MessageSeverity::Error: WI_LOG_NVRHI(Error, "{0}", messageText);
			break;
		case nvrhi::MessageSeverity::Fatal: WI_LOG_NVRHI(Critical, "{0}", messageText);
			break;
		}
	}
	Ref<IDevice> create_device(DeviceDescription desc)
	{
		switch (desc.Backend)
		{
		case nvrhi::GraphicsAPI::VULKAN: return CreateRef<VulkanDevice>(desc);
		}

		return nullptr;
	}
}