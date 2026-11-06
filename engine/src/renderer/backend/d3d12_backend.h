#pragma once

#include <vector>

#include <dxgi1_6.h>
#include <wrl/client.h>

#include <nvrhi/d3d12.h>

#include "render_backend.h"


namespace wiwa
{
	// Direct3D 12 through NVRHI: a device and one direct queue, a flip-model swapchain of three buffers (each wrapped as an
	// NVRHI texture and framebuffer), and presentation. The shaders are the same HLSL as for D3D11 (shader model 5.0, DXBC):
	// D3D12 accepts them, and NVRHI builds the root signatures from the binding layouts.
	class D3D12Backend : public IRenderBackend
	{
	public:
		~D3D12Backend() override;

		std::string get_adapter_name() const override { return m_AdapterName; }
		bool has_copy_queue() const override { return m_CopyQueue != nullptr; }

		bool initialize(const RendererDescription& description, u32 width, u32 height) override;
		void shutdown() override;

		nvrhi::IDevice* get_device() const override { return m_Device; }
		u32 get_width() const override { return m_Width; }
		u32 get_height() const override { return m_Height; }
		nvrhi::IFramebuffer* get_reference_framebuffer() const override { return m_Framebuffers.empty() ? nullptr : m_Framebuffers[0].Get(); }

		FrameStatus acquire_frame(nvrhi::IFramebuffer*& backbuffer) override;
		FrameStatus submit_and_present(nvrhi::ICommandList* commandList) override;
		bool resize(u32 width, u32 height) override;
		bool set_vsync(bool enabled) override;

	private:
		static constexpr u32 BUFFER_COUNT = 3;

		bool create_d3d_objects(const RendererDescription& description, u32 width, u32 height);
		bool create_on_adapter(const Microsoft::WRL::ComPtr<IDXGIFactory4>& factory, const Microsoft::WRL::ComPtr<IDXGIAdapter1>& adapter,
			const RendererDescription& description, HWND windowHandle, u32 width, u32 height);
		void wait_for_queue();
		bool create_backbuffers();
		void destroy_backbuffers();

	private:
		Microsoft::WRL::ComPtr<ID3D12Device> m_D3DDevice{};
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_Queue{};
		Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_CopyQueue{}; // null when the copy queue is off or could not be created
		Microsoft::WRL::ComPtr<IDXGISwapChain3> m_Swapchain{};

		// Waits for everything on the queue, the Present included (NVRHI only knows its own command lists).
		Microsoft::WRL::ComPtr<ID3D12Fence> m_Fence{};
		HANDLE m_FenceEvent{};
		UINT64 m_FenceValue{};

		nvrhi::DeviceHandle m_NvrhiDevice{};
		nvrhi::DeviceHandle m_Device{}; // validation layer if enabled, otherwise the D3D12 device

		std::vector<nvrhi::TextureHandle> m_BackbufferTextures{};
		std::vector<nvrhi::FramebufferHandle> m_Framebuffers{};

		u32 m_Width{};
		u32 m_Height{};
		std::string m_AdapterName{};
		bool m_EnableVsync{ true };
		bool m_TearingSupported{};
	};
}
