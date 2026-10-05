#pragma once
#include "rhi_common.h"
#include "rhi_resource.h"

namespace wiwa
{
    enum class PresentMode : uint8_t 
    { 
        IMMEDIATE, 
        VSYNC,
        MAILBOX
    };

    struct SwapchainDesc
    {
        void* WindowHandle = nullptr;        // HWND, NSWindow*, GLFWwindow*, ...
        Extent2D Extent{};
        Format SwapchainFormat = Format::BGRA8_UNORM;
        Format DepthFormat = Format::D32_FLOAT;
        uint32_t BufferCount = 3;
        PresentMode PresentMode = PresentMode::VSYNC;
        bool   DepthBuffer = false;
        
    };

    class ISwapchain : public IRHIResource 
    {
    public:
        virtual const SwapchainDesc& get_decription() const = 0;
        virtual void     resize(Extent2D extent) = 0;
        virtual bool     acquire_next_image() = 0;           // false = out of date / minimized
        virtual ITextureView* get_current_back_buffer_view() = 0;
        virtual ITexture* get_current_back_buffer() = 0;
        virtual ITextureView* get_depth_view() = 0;          // null if no depth
        virtual void     present() = 0;
    };
}