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

		WI_RENDER_LOG(Info, "Initializing renderer");
	}
	void Renderer::shutdown()
	{
		m_Device->shutdown();

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
		m_Device->begin_frame(deltaTime);
	}
	void Renderer::render_update(float deltaTime)
	{
		m_CommandList->open();
		nvrhi::utils::ClearColorAttachment(m_CommandList, m_Device->get_current_framebuffer(), 0, nvrhi::Color(1.0f, 0.02f, 0.05f, 1.0f));

		FrameDescription frame;
		frame.Backbuffer = m_Device->get_current_framebuffer();
		frame.Width = 1280;
		frame.Height = 720;

		for (auto& pass : m_Passes)
		{
			pass->render(m_CommandList, frame);
		}
		m_CommandList->close();

		m_Device->get_device()->executeCommandList(m_CommandList);
	}
	void Renderer::post_render_update(float deltaTime)
	{
		m_Device->end_frame(deltaTime);
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
}