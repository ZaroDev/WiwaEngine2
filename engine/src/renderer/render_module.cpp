#include <pch.h>
#include "render_module.h"

#include <GLFW/glfw3.h>

#include <assets/asset_manager.h>
#include <core/engine.h>

#include "render_world.h"
#include "window.h"

#define WI_RENDER_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	void RenderModule::init()
	{
		if (!read_arguments())
		{
			return;
		}

		Window* window = Engine::get().get_module<Window>();
		m_Description.Window = window->get_window();
		m_Description.EnableVsync = true;
		m_Description.Assets = Engine::get().get_module<AssetManager>(); // looked up here: the render thread must not touch the module map

		int width = 0;
		int height = 0;
		window->get_size(width, height);
		m_Description.Width = static_cast<u32>(std::max(width, 1));
		m_Description.Height = static_cast<u32>(std::max(height, 1));

		m_Renderer = CreateScope<Renderer>();

		const bool started = m_Thread.start(
			[this]()
			{
				return m_Renderer->initialize(m_Description);
			},
			[this](RenderBatch& batch)
			{
				// The commands first (they change the render-side state), then the frame.
				RenderWorld world(*m_Renderer);
				for (const RenderCommand& command : batch.Commands)
				{
					command(world);
				}

				if (!m_Failed && !m_Renderer->render_frame(batch))
				{
					m_Failed = true;
				}
			},
			[this]()
			{
				m_Renderer->shutdown();
				m_Renderer.reset();
			});

		if (!started)
		{
			m_Renderer.reset();
			WI_RENDER_LOG(Error, "Could not start the {0} renderer", graphics_api_name(m_Description.Api));
			Engine::get().set_exit_code(1);
			Engine::get().request_quit();
			return;
		}

		m_Started = true;
	}

	void RenderModule::shutdown()
	{
		// Closes the queue, so a frame that is waiting to be submitted does not block the quit, and lets the render thread
		// finish its frame, wait for the GPU and destroy everything before the window goes away.
		m_Thread.stop();
		m_Renderer.reset();
		m_Started = false;
		WI_RENDER_LOG(Info, "Shutdown");
	}

	void RenderModule::render_update(float deltaTime)
	{
		if (!m_Started)
		{
			return;
		}

		if (m_Failed)
		{
			fail_and_quit("The renderer failed");
			return;
		}

		Window* window = Engine::get().get_module<Window>();
		int width = 0;
		int height = 0;
		window->get_size(width, height);

		RenderBatch batch;
		batch.FrameIndex = m_FrameIndex++;
		batch.DeltaTime = deltaTime;
		batch.Width = static_cast<u32>(std::max(width, 0));
		batch.Height = static_cast<u32>(std::max(height, 0));
		batch.Minimized = width == 0 || height == 0 || glfwGetWindowAttrib(window->get_window(), GLFW_ICONIFIED) != 0;
		batch.Commands = std::move(m_PendingCommands);
		m_PendingCommands.clear();

		// Blocks while the render thread is two frames behind.
		if (!m_Thread.submit(std::move(batch)))
		{
			fail_and_quit("The render thread has stopped");
		}
	}

	bool RenderModule::read_arguments()
	{
		constexpr std::string_view apiPrefix = "--api=";

		for (const std::string& argument : Engine::get().get_arguments())
		{
			if (argument.starts_with(apiPrefix))
			{
				const std::string value = argument.substr(apiPrefix.size());
				std::optional<GraphicsApi> api = parse_graphics_api(value);
				if (!api)
				{
					WI_RENDER_LOG(Error, "Unknown --api value '{0}' (use vulkan, d3d11 or d3d12)", value);
					fail_and_quit("Invalid command line");
					return false;
				}
				m_Description.Api = *api;
			}
			else if (argument == "--gpu-validation")
			{
				m_Description.EnableValidation = true;
			}
		}

		return true;
	}

	void RenderModule::fail_and_quit(const char* reason)
	{
		WI_RENDER_LOG(Error, "{0}", reason);
		Engine::get().set_exit_code(1);
		Engine::get().request_quit();
	}
}
