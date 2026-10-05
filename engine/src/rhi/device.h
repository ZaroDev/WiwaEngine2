#pragma once
#include "rhi_common.h"
#include "rhi_resource.h"

#include "shader.h"
#include "swapchain.h"

#undef DeviceCapabilities

namespace wiwa
{
	struct DeviceCapabilities
	{
		GraphicsAPI GraphicsAPI = GraphicsAPI::NONE;
		std::string AdapterName = {};
		uint32_t MaxTextureSize2D = 0, MaxTextureSize3D = 0, MaxTextureLayers = 0;
		uint32_t MaxColorAttachments = 0, MaxSamples = 0;
		uint32_t MaxUniformBufferRange = 0, MinUniformBufferOffsetAlignment = 0;
		bool ComputeShaders = false, GeometryShaders = false, TesselationShaders = false;
		bool SupportedFormats[size_t(Format::COUNT)] = {};
	};

	struct DeviceDescription
	{
		GraphicsAPI GraphicsAPI = GraphicsAPI::NONE;
		bool EnableValidationLayers = false;
		bool EnableDebugMarkers = false;
		void* WindowHandle = nullptr;
		uint32_t FramesInFlight = 3;
	};

	class IBuffer;
	class IDevice
	{
	public:
		static Ref<IDevice> create_device(const DeviceDescription& desc);

		virtual ~IDevice() = default;
		virtual GraphicsAPI get_graphics_api() const = 0;
		virtual const DeviceCapabilities& get_capabilities() const = 0;

		//Resources
		virtual Ref<IBuffer> create_buffer(const BufferDescription& desc) = 0;
		virtual Ref<ITexture> create_texture(const TextureDescription& desc) = 0;
		virtual Ref<ISampler> create_sampler(const SamplerDescription& desc) = 0;

		//Shaders
		virtual Ref<IShader> create_shader(const ShaderDescription& desc) = 0;
		virtual IShaderCompiler* get_shader_compiler() = 0;

		//Pipelines
		virtual Ref<IBindingLayout> create_binding_layout(const BindingLayoutDescription& desc) = 0;
		virtual Ref<IPipelineLayout> create_pipeline_layout(const PipelineLayoutDescription& desc) = 0;
		virtual Ref<IBindingSet> create_binding_set(const BindingSetDescription& desc) = 0;
		virtual Ref<IGraphicsPipeline> create_graphics_pipeline(const GraphicsPipelineDescription& desc) = 0;
		virtual Ref<IComputePipeline> create_compute_pipeline(const ComputePipelineDescription& desc) = 0;


		// Presentation
		virtual Ref<ISwapChain> create_swapchain(const SwapchainDescription& desc) = 0;
		virtual Ref<ICommandList> create_command_list(QueueType type = QueueType::GRAPHICS) = 0;
		virtual IQueue* get_queue(QueueType type = QueueType::GRAPHICS) = 0;
		virtual Ref<IFence> create_fence(uint64_t initialValue = 0) = 0;
		virtual Ref<IQueryPool> create_timespamp_query_pool(uint32_t count) = 0;

		//Immediate uploads
		virtual void update_buffer(IBuffer* dest, uint64_t offset, const void* data, uint64_t size) = 0;
		virtual void update_texture(ITexture* dest, const TextureSubresource& resource, const void* data, uint32_t rowPitch, uint32_t slicePitch) = 0;

		//Frame life-cycle
		virtual void begin_frame() = 0;
		virtual void end_frame() = 0;
		virtual void wait_for_idle() = 0;
	};
}
