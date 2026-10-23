#pragma once
#include <core/module.h>
#include "device.h"

#include <deque>

namespace wiwa
{
	class Renderer : public IModule
	{
	public:
		Renderer() = default;
		virtual ~Renderer() override = default;

		virtual void init() override;
		virtual void shutdown() override;

		virtual void pre_update(float deltaTime)override;
		virtual void update(float deltaTime) override;
		virtual void post_update(float deltaTime) override;

		virtual void pre_render_update(float deltaTime) override;
		virtual void render_update(float deltaTime) override;
		virtual void post_render_update(float deltaTime) override;

		virtual UpdateFlags get_update_flags() const { return UpdateFlags::UPDATE | UpdateFlags::UPDATE_RENDERER; }
		virtual std::string get_name() const { return "Renderer"; };

		virtual int get_priority() const { return 0; }

	private:
		bool add_pass(Ref<IRenderPass> pass);

		void wait_for_frame_slot();
		void mark_frame_submitted();

		bool resize_device(u32 width, u32 height);

		u64 m_FrameIndex = 0;

		Ref<IDevice> m_Device;
		nvrhi::CommandListHandle m_CommandList;
		std::vector<Ref<IRenderPass>> m_Passes;

		std::deque<nvrhi::EventQueryHandle> m_FramesInFlight;
		std::vector<nvrhi::EventQueryHandle> m_QueryPool;

		class Window* m_Window;
	};
}