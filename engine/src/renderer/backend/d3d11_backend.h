#pragma once

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <nvrhi/d3d11.h>

#include "render_backend.h"


namespace wiwa
{
	class D3D11Backend : public IRenderBackend
	{
	public:
		~D3D11Backend() override;

		std::string get_adapter_name() const override { return m_AdapterName; }

		bool initialize(const RendererDescription& description, u32 width, u32 height) override;
		void shutdown() override;

		nvrhi::IDevice* get_device() const override { return m_Device; }
		u32 get_width() const override { return m_Width; }
		u32 get_height() const override { return m_Height; }
		nvrhi::IFramebuffer* get_reference_framebuffer() const override { return m_Framebuffer; }

		FrameStatus acquire_frame(nvrhi::IFramebuffer*& backbuffer) override;
		FrameStatus submit_and_present(nvrhi::ICommandList* commandList) override;
		bool resize(u32 width, u32 height) override;
		bool set_vsync(bool enabled) override;

	private:
		bool create_d3d_objects(const RendererDescription& description, u32 width, u32 height);
		bool create_backbuffer();
		void drain_debug_messages();
		void destroy_backbuffer();

	private:
		Microsoft::WRL::ComPtr<ID3D11Device> m_D3DDevice{};
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_Context{};
		Microsoft::WRL::ComPtr<IDXGISwapChain1> m_Swapchain{};
		Microsoft::WRL::ComPtr<ID3D11InfoQueue> m_InfoQueue{}; // the debug layer's messages, when validation is on

		nvrhi::DeviceHandle m_NvrhiDevice{};
		nvrhi::DeviceHandle m_Device{}; // validation layer if enabled, otherwise the D3D11 device

		// Flip-model swapchains only expose buffer 0 to the app, so there is a single backbuffer.
		nvrhi::TextureHandle m_BackbufferTexture{};
		nvrhi::FramebufferHandle m_Framebuffer{};

		u32 m_Width{};
		u32 m_Height{};
		std::string m_AdapterName{};
		bool m_EnableVsync{ true };
		bool m_TearingSupported{}; // lets a windowed flip-model swapchain present uncapped with vsync off
	};
}
