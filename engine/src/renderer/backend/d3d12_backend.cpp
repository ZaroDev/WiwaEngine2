#include <pch.h>
#include "d3d12_backend.h"

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

		// Prints "D3D12Backend: <what> failed (0x...)" when result is a failure. Returns true on success.
		bool check(HRESULT result, const char* what)
		{
			if (SUCCEEDED(result))
			{
				return true;
			}
			WI_LOG(Error, "D3D12", "{0} failed (0x{1:x})", what, static_cast<unsigned long>(result));
			return false;
		}

		// The debug layer's warnings and errors on the console (without a debugger attached they would be lost).
		void register_debug_messages(ID3D12Device* device)
		{
			ComPtr<ID3D12InfoQueue1> infoQueue;
			if (FAILED(device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
			{
				return;
			}

			// Every clear of a depth or color target that was made without a "clear value" warns once per clear. The clears are
			// correct (they are just not on the fast path some GPUs have), and there are thousands of them.
			D3D12_MESSAGE_ID denied[] = {
				D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
				D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
			};
			D3D12_INFO_QUEUE_FILTER filter{};
			filter.DenyList.NumIDs = static_cast<UINT>(std::size(denied));
			filter.DenyList.pIDList = denied;
			infoQueue->PushStorageFilter(&filter);

			DWORD cookie = 0;
			infoQueue->RegisterMessageCallback(
				[](D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID, LPCSTR text, void*)
				{
					if (severity <= D3D12_MESSAGE_SEVERITY_ERROR)
					{
						WI_LOG(Error, "D3D12", "{0}", text);
						report_backend_error();
					}
					else if (severity == D3D12_MESSAGE_SEVERITY_WARNING)
					{
						WI_LOG(Warning, "D3D12", "{0}", text);
					}
				},
				D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &cookie);
		}
	}

	D3D12Backend::~D3D12Backend()
	{
		shutdown();
	}

	bool D3D12Backend::initialize(const RendererDescription& description, u32 width, u32 height)
	{
		m_EnableVsync = description.EnableVsync;
		m_Width = width;
		m_Height = height;

		if (!create_d3d_objects(description, width, height))
		{
			return false;
		}

		nvrhi::d3d12::DeviceDesc deviceDescription;
		deviceDescription.errorCB = get_nvrhi_message_callback();
		deviceDescription.pDevice = m_D3DDevice.Get();
		deviceDescription.pGraphicsCommandQueue = m_Queue.Get();
		deviceDescription.pCopyCommandQueue = m_CopyQueue.Get();

		m_NvrhiDevice = nvrhi::d3d12::createDevice(deviceDescription);
		if (!m_NvrhiDevice)
		{
			WI_LOG(Error, "D3D12", "failed to create the NVRHI D3D12 device");
			return false;
		}

		m_Device = description.EnableValidation
			? nvrhi::validation::createValidationLayer(m_NvrhiDevice)
			: m_NvrhiDevice;

		return create_backbuffers();
	}

	void D3D12Backend::shutdown()
	{
		if (m_D3DDevice == nullptr)
		{
			return;
		}

		if (m_Device)
		{
			m_Device->waitForIdle();
			wait_for_queue(); // the last Present is still on the queue
			destroy_backbuffers();
			m_Device->runGarbageCollection();
		}

		m_Device = nullptr;
		m_NvrhiDevice = nullptr;

		m_Swapchain.Reset();
		m_Fence.Reset();
		if (m_FenceEvent != nullptr)
		{
			CloseHandle(m_FenceEvent);
			m_FenceEvent = nullptr;
		}
		m_Queue.Reset();
		m_CopyQueue.Reset();
		m_D3DDevice.Reset();
	}

	FrameStatus D3D12Backend::acquire_frame(nvrhi::IFramebuffer*& backbuffer)
	{
		const UINT index = m_Swapchain->GetCurrentBackBufferIndex();
		backbuffer = m_Framebuffers[index];
		return FrameStatus::Ok;
	}

	FrameStatus D3D12Backend::submit_and_present(nvrhi::ICommandList* commandList)
	{
		m_LastGraphicsInstance = m_Device->executeCommandList(commandList);

		const UINT presentFlags = (!m_EnableVsync && m_TearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0;
		if (!check(m_Swapchain->Present(m_EnableVsync ? 1 : 0, presentFlags), "Present"))
		{
			return FrameStatus::Failed;
		}

		m_Device->runGarbageCollection();
		return FrameStatus::Ok;
	}

	bool D3D12Backend::resize(u32 width, u32 height)
	{
		// The swapchain can only be resized once nothing references its buffers (the queued Present included).
		m_Device->waitForIdle();
		wait_for_queue();
		destroy_backbuffers();
		m_Device->runGarbageCollection();

		// The flags must be the ones the swapchain was created with.
		const UINT swapchainFlags = m_TearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
		if (!check(m_Swapchain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, swapchainFlags), "ResizeBuffers"))
		{
			return false;
		}

		m_Width = width;
		m_Height = height;
		return create_backbuffers();
	}

	bool D3D12Backend::set_vsync(bool enabled)
	{
		m_EnableVsync = enabled; // the sync interval is a Present argument: nothing to recreate
		return false;
	}

	void D3D12Backend::wait_for_queue()
	{
		if (m_Queue == nullptr || m_Fence == nullptr)
		{
			return;
		}

		// NVRHI waits for its own command lists; this also covers what it does not know about: the Present.
		const UINT64 value = ++m_FenceValue;
		if (SUCCEEDED(m_Queue->Signal(m_Fence.Get(), value)) && m_Fence->GetCompletedValue() < value)
		{
			if (SUCCEEDED(m_Fence->SetEventOnCompletion(value, m_FenceEvent)))
			{
				WaitForSingleObject(m_FenceEvent, INFINITE);
			}
		}
	}

	bool D3D12Backend::create_d3d_objects(const RendererDescription& description, u32 width, u32 height)
	{
		HWND windowHandle = glfwGetWin32Window(description.Window);

		UINT factoryFlags = 0;
		if (description.EnableValidation)
		{
			// The debug layer is an optional Windows component ("Graphics Tools"): without it the device is made without it.
			ComPtr<ID3D12Debug> debug;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
			{
				debug->EnableDebugLayer();
				factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
			}
			else
			{
				WI_LOG(Error, "D3D12", "debug layer unavailable, continuing without it");
			}
		}

		ComPtr<IDXGIFactory4> factory;
		if (!check(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&factory)), "CreateDXGIFactory2"))
		{
			return false;
		}

		// The fastest GPU first when the system can tell (Windows 10 1803+), then the others. On a laptop with hybrid graphics the fastest
		// GPU may drive no display, and DXGI then refuses to make a swapchain for the window on it (DXGI_ERROR_DEVICE_REMOVED): the next
		// adapter is tried. Software adapters are never used.
		std::vector<ComPtr<IDXGIAdapter1>> candidates;
		const auto add_candidate = [&](const ComPtr<IDXGIAdapter1>& adapter)
		{
			DXGI_ADAPTER_DESC1 description{};
			if (adapter == nullptr || FAILED(adapter->GetDesc1(&description)) || (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
			{
				return;
			}
			for (const ComPtr<IDXGIAdapter1>& known : candidates)
			{
				DXGI_ADAPTER_DESC1 knownDescription{};
				if (SUCCEEDED(known->GetDesc1(&knownDescription)) && knownDescription.AdapterLuid.HighPart == description.AdapterLuid.HighPart
					&& knownDescription.AdapterLuid.LowPart == description.AdapterLuid.LowPart)
				{
					return;
				}
			}
			candidates.push_back(adapter);
		};

		ComPtr<IDXGIFactory6> factory6;
		if (SUCCEEDED(factory.As(&factory6)))
		{
			ComPtr<IDXGIAdapter1> fastest;
			factory6->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&fastest));
			add_candidate(fastest);
		}
		ComPtr<IDXGIAdapter1> adapter;
		for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index)
		{
			add_candidate(adapter);
			adapter.Reset();
		}

		if (candidates.empty())
		{
			WI_LOG(Error, "D3D12", "no hardware adapter");
			return false;
		}

		for (size_t i = 0; i < candidates.size(); ++i)
		{
			if (create_on_adapter(factory, candidates[i], description, windowHandle, width, height))
			{
				return true;
			}

			// Everything the attempt made is dropped before the next one.
			m_Swapchain.Reset();
			m_CopyQueue.Reset();
			m_Fence.Reset();
			if (m_FenceEvent != nullptr)
			{
				CloseHandle(m_FenceEvent);
				m_FenceEvent = nullptr;
			}
			m_Queue.Reset();
			m_D3DDevice.Reset();
			if (i + 1 < candidates.size())
			{
				WI_LOG(Warning, "D3D12", "'{0}' could not be used, trying the next adapter", m_AdapterName);
			}
		}
		WI_LOG(Error, "D3D12", "no adapter could present to the window");
		return false;
	}

	// The device, the queues, the fence and the swapchain of the window, all on one adapter.
	bool D3D12Backend::create_on_adapter(const ComPtr<IDXGIFactory4>& factory, const ComPtr<IDXGIAdapter1>& adapter, const RendererDescription& description,
		HWND windowHandle, u32 width, u32 height)
	{
		DXGI_ADAPTER_DESC1 adapterDescription{};
		if (SUCCEEDED(adapter->GetDesc1(&adapterDescription)))
		{
			m_AdapterName = to_utf8(adapterDescription.Description);
		}

		if (!check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_D3DDevice)), "D3D12CreateDevice"))
		{
			return false;
		}

		if (description.EnableValidation)
		{
			register_debug_messages(m_D3DDevice.Get());
		}

		D3D12_COMMAND_QUEUE_DESC queueDescription{};
		queueDescription.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
		if (!check(m_D3DDevice->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&m_Queue)), "CreateCommandQueue")
			|| !check(m_D3DDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_Fence)), "CreateFence"))
		{
			return false;
		}
		m_FenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);

		if (description.EnableCopyQueue)
		{
			D3D12_COMMAND_QUEUE_DESC copyDescription{};
			copyDescription.Type = D3D12_COMMAND_LIST_TYPE_COPY;
			if (FAILED(m_D3DDevice->CreateCommandQueue(&copyDescription, IID_PPV_ARGS(&m_CopyQueue))))
			{
				WI_LOG(Warning, "D3D12", "no copy queue, uploads use the graphics queue");
				m_CopyQueue.Reset();
			}
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
		swapchainDescription.BufferCount = BUFFER_COUNT;
		swapchainDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		swapchainDescription.Flags = m_TearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

		ComPtr<IDXGISwapChain1> swapchain1;
		// A warning, not an error: create_d3d_objects goes on with the next adapter.
		const HRESULT swapchainResult = factory->CreateSwapChainForHwnd(m_Queue.Get(), windowHandle, &swapchainDescription, nullptr, nullptr, &swapchain1);
		if (FAILED(swapchainResult))
		{
			WI_LOG(Warning, "D3D12", "CreateSwapChainForHwnd failed on '{0}' (0x{1:x})", m_AdapterName, static_cast<unsigned long>(swapchainResult));
			return false;
		}
		if (!check(swapchain1.As(&m_Swapchain), "IDXGISwapChain3"))
		{
			return false;
		}

		factory->MakeWindowAssociation(windowHandle, DXGI_MWA_NO_ALT_ENTER);
		return true;
	}

	bool D3D12Backend::create_backbuffers()
	{
		for (u32 i = 0; i < BUFFER_COUNT; i++)
		{
			ComPtr<ID3D12Resource> buffer;
			if (!check(m_Swapchain->GetBuffer(i, IID_PPV_ARGS(&buffer)), "GetBuffer"))
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

			nvrhi::TextureHandle texture = m_Device->createHandleForNativeTexture(
				nvrhi::ObjectTypes::D3D12_Resource, nvrhi::Object(buffer.Get()), textureDescription);

			nvrhi::FramebufferDesc framebufferDescription;
			framebufferDescription.addColorAttachment(texture);
			nvrhi::FramebufferHandle framebuffer = m_Device->createFramebuffer(framebufferDescription);
			if (!texture || !framebuffer)
			{
				WI_LOG(Error, "D3D12", "failed to wrap the backbuffer {0}", i);
				return false;
			}

			m_BackbufferTextures.push_back(std::move(texture));
			m_Framebuffers.push_back(std::move(framebuffer));
		}
		return true;
	}

	void D3D12Backend::destroy_backbuffers()
	{
		m_Framebuffers.clear();
		m_BackbufferTextures.clear();
	}
}
