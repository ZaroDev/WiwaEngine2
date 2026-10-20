#pragma once
#include <nvrhi/nvrhi.h>

namespace wiwa
{
	struct FrameDescription
	{
		nvrhi::IFramebuffer* Backbuffer = nullptr;
		uint32_t Width = 0;
		uint32_t Height = 0;

		glm::vec3 CameraPosition = glm::vec3(0.0f);
		glm::mat4 View = glm::mat4(1.0f);
		glm::mat4 Projection = glm::mat4(1.0f);
		glm::mat4 ViewProjection = glm::mat4(1.0f);
	};


	class IRenderPass
	{
	public:
		virtual ~IRenderPass() = default;

		virtual bool initialize(nvrhi::IDevice* device, nvrhi::IFramebuffer* framebuffer) = 0;
		virtual void on_resize(nvrhi::IDevice* device, uint32_t width, uint32_t height) {}
		virtual void render(nvrhi::ICommandList* command_list, const FrameDescription& frame) = 0;
	};
}