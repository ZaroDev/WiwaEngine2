#include <pch.h>
#include "render_backend.h"

#include <atomic>

#include "vulkan_backend.h"
#include "d3d11_backend.h"
#include "d3d12_backend.h"

#define WI_BACKEND_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		std::atomic<u32> g_ErrorMessageCount{ 0 };

		class MessageCallback : public nvrhi::IMessageCallback
		{
		public:
			void message(nvrhi::MessageSeverity severity, const char* messageText) override
			{
				switch (severity)
				{
				case nvrhi::MessageSeverity::Info:
					WI_LOG(Info, "NVRHI", "{0}", messageText);
					break;
				case nvrhi::MessageSeverity::Warning:
					WI_LOG(Warning, "NVRHI", "{0}", messageText);
					break;
				case nvrhi::MessageSeverity::Error:
					WI_LOG(Error, "NVRHI", "{0}", messageText);
					report_backend_error();
					break;
				case nvrhi::MessageSeverity::Fatal:
					WI_LOG(Critical, "NVRHI", "{0}", messageText);
					report_backend_error();
					break;
				}
			}
		};

		MessageCallback g_MessageCallback{};

		bool equals_ignore_case(std::string_view a, std::string_view b)
		{
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y)
				{
					return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
				});
		}
	}

	std::string_view graphics_api_name(GraphicsApi api)
	{
		switch (api)
		{
		case GraphicsApi::Vulkan: return "vulkan";
		case GraphicsApi::D3D11: return "d3d11";
		case GraphicsApi::D3D12: return "d3d12";
		}
		return "unknown";
	}

	std::optional<GraphicsApi> parse_graphics_api(std::string_view name)
	{
		for (GraphicsApi api : { GraphicsApi::Vulkan, GraphicsApi::D3D11, GraphicsApi::D3D12 })
		{
			if (equals_ignore_case(name, graphics_api_name(api)))
			{
				return api;
			}
		}
		return std::nullopt;
	}

	nvrhi::IMessageCallback* get_nvrhi_message_callback()
	{
		return &g_MessageCallback;
	}

	u32 get_error_message_count()
	{
		return g_ErrorMessageCount.load();
	}

	void report_backend_error()
	{
		g_ErrorMessageCount++;
	}

	Scope<IRenderBackend> create_render_backend(GraphicsApi api)
	{
		switch (api)
		{
		case GraphicsApi::Vulkan: return CreateScope<VulkanBackend>();
		case GraphicsApi::D3D11: return CreateScope<D3D11Backend>();
		case GraphicsApi::D3D12: return CreateScope<D3D12Backend>();
		}

		WI_BACKEND_LOG(Error, "Unknown graphics API");
		return nullptr;
	}
}
