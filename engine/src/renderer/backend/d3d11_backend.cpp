#include <pch.h>
#include "d3d11_backend.h"

#include <dxgi1_5.h>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <nvrhi/validation.h>

namespace wiwa
{
	using Microsoft::WRL::ComPtr;

	namespace
	{
		// UTF-16 adapter names from DXGI to UTF-8.
		std::string to_utf8(const wchar_t* text)
		{
			const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
			if (size <= 1)
			{
				return {};
			}
			std::string result(static_cast<size_t>(size - 1), '\0');
			WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
			return result;
		}

		// Prints "D3D11Backend: <what> failed (0x...)" when result is a failure. Returns true on success.
		bool check(HRESULT result, const char* what)
		{
			if (SUCCEEDED(result))
			{
				return true;
			}
			WI_LOG(Error, "D3D11", "{0} failed (0x{1:x})", what, static_cast<unsigned long>(result));
			return false;
		}
	}

	D3D11Backend::~D3D11Backend()
	{
		shutdown();
	}

	bool D3D11Backend::initialize(const RendererDescription& description, u32 width, u32 height)
	{
		m_EnableVsync = description.EnableVsync;
		m_Width = width;
		m_Height = height;

		if (!create_d3d_objects(description, width, height))
		{
			return false;
		}

		nvrhi::d3d11::DeviceDesc deviceDescription;
		deviceDescription.messageCallback = get_nvrhi_message_callback();
		deviceDescription.context = m_Context.Get();

		m_NvrhiDevice = nvrhi::d3d11::createDevice(deviceDescription);
		if (!m_NvrhiDevice)
		{
			WI_LOG(Error, "D3D11", "failed to create NVRHI D3D11 device");
			return false;
		}

		m_Device = description.EnableValidation
			? nvrhi::validation::createValidationLayer(m_NvrhiDevice)
			: m_NvrhiDevice;

		return create_backbuffer();
	}

	void D3D11Backend::shutdown()
	{
		if (m_D3DDevice == nullptr)
		{
			return;
		}

		if (m_Device)
		{
			m_Device->waitForIdle();
			destroy_backbuffer();
			m_Device->runGarbageCollection();
		}

		m_Device = nullptr;
		m_NvrhiDevice = nullptr;

		m_Swapchain.Reset();
		m_InfoQueue.Reset();
		if (m_Context)
		{
			m_Context->ClearState();
			m_Context->Flush();
		}
		m_Context.Reset();
		m_D3DDevice.Reset();
	}

	FrameStatus D3D11Backend::acquire_frame(nvrhi::IFramebuffer*& backbuffer)
	{
		backbuffer = m_Framebuffer;
		return FrameStatus::Ok;
	}

	FrameStatus D3D11Backend::submit_and_present(nvrhi::ICommandList* commandList)
	{
		m_Device->executeCommandList(commandList);

		const UINT presentFlags = (!m_EnableVsync && m_TearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0;
		if (!check(m_Swapchain->Present(m_EnableVsync ? 1 : 0, presentFlags), "Present"))
		{
			return FrameStatus::Failed;
		}

		drain_debug_messages();
		m_Device->runGarbageCollection();
		return FrameStatus::Ok;
	}

	bool D3D11Backend::resize(u32 width, u32 height)
	{
		// The swapchain can only be resized once nothing references its buffer.
		m_Device->waitForIdle();
		destroy_backbuffer();
		m_Device->runGarbageCollection();

		m_Context->ClearState();
		m_Context->Flush();

		// The flags must be the ones the swapchain was created with.
		const UINT swapchainFlags = m_TearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
		if (!check(m_Swapchain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, swapchainFlags), "ResizeBuffers"))
		{
			return false;
		}

		m_Width = width;
		m_Height = height;
		return create_backbuffer();
	}

	bool D3D11Backend::set_vsync(bool enabled)
	{
		m_EnableVsync = enabled; // the sync interval is a Present argument: nothing to recreate
		return false;
	}

	bool D3D11Backend::create_d3d_objects(const RendererDescription& description, u32 width, u32 height)
	{
		HWND windowHandle = glfwGetWin32Window(description.Window);

		const D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };

		auto createDevice = [&](UINT flags)
		{
			return D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
				featureLevels, static_cast<UINT>(std::size(featureLevels)), D3D11_SDK_VERSION,
				&m_D3DDevice, nullptr, &m_Context);
		};

		UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
		if (description.EnableValidation)
		{
			flags |= D3D11_CREATE_DEVICE_DEBUG;
		}

		HRESULT result = createDevice(flags);
		if (FAILED(result) && (flags & D3D11_CREATE_DEVICE_DEBUG))
		{
			// The debug layer is an optional Windows component ("Graphics Tools").
			WI_LOG(Error, "D3D11", "debug layer unavailable, continuing without it");
			result = createDevice(flags & ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG));
		}
		if (!check(result, "D3D11CreateDevice"))
		{
			return false;
		}

		if (description.EnableValidation)
		{
			m_D3DDevice.As(&m_InfoQueue); // null when the debug layer is not installed
		}

		ComPtr<IDXGIDevice> dxgiDevice;
		ComPtr<IDXGIAdapter> adapter;
		ComPtr<IDXGIFactory2> factory;
		if (FAILED(m_D3DDevice.As(&dxgiDevice)) ||
			FAILED(dxgiDevice->GetAdapter(&adapter)) ||
			FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))))
		{
			WI_LOG(Error, "D3D11", "failed to get the DXGI factory");
			return false;
		}

		DXGI_ADAPTER_DESC adapterDescription{};
		if (SUCCEEDED(adapter->GetDesc(&adapterDescription)))
		{
			m_AdapterName = to_utf8(adapterDescription.Description);
		}

		// Windows 10+ with a recent driver: without it, presenting with vsync off is still capped by the compositor.
		ComPtr<IDXGIFactory5> factory5;
		if (SUCCEEDED(factory.As(&factory5)))
		{
			BOOL allowTearing = FALSE;
			m_TearingSupported = SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing)))
				&& allowTearing;
		}

		DXGI_SWAP_CHAIN_DESC1 swapchainDescription{};
		swapchainDescription.Width = width;
		swapchainDescription.Height = height;
		swapchainDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		swapchainDescription.SampleDesc.Count = 1;
		swapchainDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapchainDescription.BufferCount = 2;
		swapchainDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapchainDescription.Flags = m_TearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

		if (!check(factory->CreateSwapChainForHwnd(m_D3DDevice.Get(), windowHandle, &swapchainDescription, nullptr, nullptr, &m_Swapchain),
			"CreateSwapChainForHwnd"))
		{
			return false;
		}

		factory->MakeWindowAssociation(windowHandle, DXGI_MWA_NO_ALT_ENTER);
		return true;
	}

	bool D3D11Backend::create_backbuffer()
	{
		ComPtr<ID3D11Texture2D> buffer;
		if (!check(m_Swapchain->GetBuffer(0, IID_PPV_ARGS(&buffer)), "GetBuffer"))
		{
			return false;
		}

		nvrhi::TextureDesc textureDescription;
		textureDescription.width = m_Width;
		textureDescription.height = m_Height;
		textureDescription.format = nvrhi::Format::BGRA8_UNORM;
		textureDescription.debugName = "Swapchain backbuffer";
		textureDescription.isRenderTarget = true;
		textureDescription.initialState = nvrhi::ResourceStates::Present;
		textureDescription.keepInitialState = true;

		m_BackbufferTexture = m_Device->createHandleForNativeTexture(
			nvrhi::ObjectTypes::D3D11_Resource, nvrhi::Object(buffer.Get()), textureDescription);

		nvrhi::FramebufferDesc framebufferDescription;
		framebufferDescription.addColorAttachment(m_BackbufferTexture);
		m_Framebuffer = m_Device->createFramebuffer(framebufferDescription);

		return m_Framebuffer != nullptr;
	}

	void D3D11Backend::destroy_backbuffer()
	{
		m_Framebuffer = nullptr;
		m_BackbufferTexture = nullptr;
	}

	void D3D11Backend::drain_debug_messages()
	{
		if (m_InfoQueue == nullptr)
		{
			return;
		}

		const UINT64 count = m_InfoQueue->GetNumStoredMessages();
		for (UINT64 i = 0; i < count; i++)
		{
			SIZE_T size = 0;
			if (FAILED(m_InfoQueue->GetMessage(i, nullptr, &size)))
			{
				continue;
			}

			std::vector<char> storage(size);
			D3D11_MESSAGE* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
			if (FAILED(m_InfoQueue->GetMessage(i, message, &size)))
			{
				continue;
			}

			if (message->Severity <= D3D11_MESSAGE_SEVERITY_ERROR)
			{
				WI_LOG(Error, "D3D11", "{0}", message->pDescription);
				report_backend_error();
			}
			else if (message->Severity == D3D11_MESSAGE_SEVERITY_WARNING)
			{
				WI_LOG(Warning, "D3D11", "{0}", message->pDescription);
			}
		}
		m_InfoQueue->ClearStoredMessages();
	}
}
