#pragma once

#include "../render_pass.h"
#include "../shader_library.h"

namespace wiwa
{
	// Sorts the scene's point and spot lights into the cluster grid (16 x 9 screen tiles times 24 exponential depth slices of the
	// camera's view) with a compute shader: for every cluster, the indices of the lights that reach it. The forward pass reads the
	// lists. Does nothing without lights or a camera.
	class LightCullPass : public IRenderPass
	{
	public:
		virtual const char* get_name() const override { return "Light culling"; }

		virtual bool initialize(const PassInitializationDescription& description) override;
		virtual void update(nvrhi::IDevice* device, nvrhi::IFramebuffer* backbuffer) override;
		virtual bool is_ready() const override;
		virtual bool has_failed() const override { return m_Failed; }
		virtual void render(nvrhi::ICommandList* commandList, const FrameDescription& frame) override;

	private:
		nvrhi::IDevice* m_Device = nullptr;
		ShaderLibrary* m_Shaders = nullptr;
		bool m_Failed = false;

		nvrhi::BindingLayoutHandle m_Layout{};
		nvrhi::BindingSetHandle m_BindingSet{};
		nvrhi::ComputePipelineHandle m_Pipeline{};
		u32 m_BuiltShaderGeneration = ~0u;
	};
}
