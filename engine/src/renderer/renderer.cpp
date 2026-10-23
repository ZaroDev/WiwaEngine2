#include <pch.h>
#include "renderer.h"
#include <nvrhi/utils.h>

#include "render_passes/imgui_pass.h"

#include <core/engine.h>
#include <renderer/window.h>


#define WI_RENDER_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	void Renderer::init()
	{
		DeviceDescription deviceDesc;
		deviceDesc.Backend = nvrhi::GraphicsAPI::VULKAN;

		m_Device = create_device(deviceDesc);

		m_Device->initialize();
		m_CommandList = m_Device->get_device()->createCommandList();

		ImGuiPassDescription ui_description;
		ui_description.Window = Engine::get().get_module<Window>()->get_window();
		ui_description.BuildUi = [&]()
			{
				const ImGuiIO& io = ImGui::GetIO();

				ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_FirstUseEver);
				ImGui::Begin("Renderer");
				ImGui::Text("%.1f FPS (%.2f ms)", io.Framerate, 1000.0f / io.Framerate);
				static bool showDemoWindow = false;
				ImGui::Checkbox("ImGui demo window", &showDemoWindow);
				ImGui::End();

				if (showDemoWindow)
				{
					ImGui::ShowDemoWindow(&showDemoWindow);
				}
			};

		
		add_pass(CreateRef<ImGuiPass>(ui_description));

		m_Window = Engine::get().get_module<Window>();

		WI_RENDER_LOG(Info, "Initializing renderer");
	}
	void Renderer::shutdown()
	{
		if (nvrhi::IDevice* device = m_Device->get_device())
		{
			device->waitForIdle();
		}

		m_Passes.clear();
		m_CommandList = nullptr;
		m_FramesInFlight.clear();
		m_QueryPool.clear();

		m_Device->shutdown();
		m_Device.reset();
	

		WI_RENDER_LOG(Info, "Shutdown");
	}
	void Renderer::pre_update(float deltaTime)
	{
	}
	void Renderer::update(float deltaTime)
	{
	}
	void Renderer::post_update(float deltaTime)
	{
	}
	void Renderer::pre_render_update(float deltaTime)
	{
		
	}
	void Renderer::render_update(float deltaTime)
	{
		int width, height;
		m_Window->get_size(width, height);

		if (width != m_Device->get_width() || height != m_Device->get_height())
		{
			resize_device(width, height);
			return;
		}

		wait_for_frame_slot();

		nvrhi::IFramebuffer* backbuffer = nullptr;
		FrameStatus status = m_Device->adquire_frame(backbuffer);

		for (auto& pass : m_Passes)
		{
			pass->update(m_Device->get_device(), m_Device->get_current_framebuffer());
		}
		
		if (status == FrameStatus::RESIZE_NEEDED)
		{
			resize_device(width, height);
			return;
		}

		if (status == FrameStatus::FAILED)
		{
			return;
		}

		m_CommandList->open();

		m_CommandList->clearState();

		FrameDescription frame;
		frame.Backbuffer = backbuffer;
		frame.Width = m_Device->get_width();
		frame.Height = m_Device->get_height();
		
		for (auto& pass : m_Passes)
		{
			pass->render(m_CommandList, frame);
		}

		m_CommandList->close();

		status = m_Device->submit_and_present(m_CommandList);

		if (status != FrameStatus::FAILED)
		{
			mark_frame_submitted();
		}
	}
	void Renderer::post_render_update(float deltaTime)
	{
	
	}
	bool Renderer::add_pass(Ref<IRenderPass> pass)
	{

		if (!pass->initialize(m_Device->get_device(), m_Device->get_current_framebuffer()))
		{
			return false;
		}

		m_Passes.push_back(std::move(pass));
		return true;
	}
	void Renderer::wait_for_frame_slot()
	{
		nvrhi::IDevice* device = m_Device->get_device();

		while (m_FramesInFlight.size() >= m_Device->get_description().FramesInFlight)
		{
			nvrhi::EventQueryHandle query = m_FramesInFlight.front();
			m_FramesInFlight.pop_front();

			device->waitEventQuery(query);
			m_QueryPool.push_back(query);
		}
	}
	void Renderer::mark_frame_submitted()
	{
		nvrhi::IDevice* device = m_Device->get_device();

		nvrhi::EventQueryHandle query;
		if (!m_QueryPool.empty())
		{
			query = m_QueryPool.back();
			m_QueryPool.pop_back();
		}
		else
		{
			query = device->createEventQuery();
		}

		device->resetEventQuery(query);
		device->setEventQuery(query, nvrhi::CommandQueue::Graphics);
		m_FramesInFlight.push_back(query);
	}
	bool Renderer::resize_device(u32 width, u32 height)
	{
		if (!m_Device->update_swapchain(width, height))
		{
			return false;
		}

		for (auto& pass : m_Passes)
		{
			pass->on_resize(m_Device->get_device(), m_Device->get_width(), m_Device->get_height());
		}

		return true;
	}
}