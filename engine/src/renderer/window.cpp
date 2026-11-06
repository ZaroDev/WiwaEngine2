#include <pch.h>
#include "window.h"

#include <GLFW/glfw3.h>

#include <iostream>

#include <format>
#include <core/engine.h>


namespace wiwa
{
	static void glfw_error_callback(int error, const char* description)
	{
		WI_ERROR("GLFW", "{} : {}", error, description);
	}

	void Window::init()
	{
		glfwInit();
		glfwSetErrorCallback(glfw_error_callback);

#ifndef WI_DIST
		glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

		// A game takes its window from the project in its pack; the editor owns its window and only sets the title later.
		const Engine& engine = Engine::get();
		const ProjectSettings& project = engine.project();
		const bool fromProject = engine.project_from_pak();

		std::string windowTitle = fromProject ? window_title(project) : fmt::format("Wiwa 2 <{0}>", WI_BUILD_CONFIGURATION);
		int width = fromProject ? static_cast<int>(project.WindowWidth) : 1280;
		int height = fromProject ? static_cast<int>(project.WindowHeight) : 720;
		GLFWmonitor* monitor = nullptr;
		if (fromProject)
		{
			glfwWindowHint(GLFW_RESIZABLE, project.Resizable ? GLFW_TRUE : GLFW_FALSE);
			if (project.Mode != WindowMode::Windowed)
			{
				monitor = glfwGetPrimaryMonitor();
				const GLFWvidmode* mode = monitor != nullptr ? glfwGetVideoMode(monitor) : nullptr;
				if (mode != nullptr)
				{
					width = mode->width;
					height = mode->height;
					if (project.Mode == WindowMode::Borderless)
					{
						glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
						glfwWindowHint(GLFW_RED_BITS, mode->redBits);
						glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
						glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
						glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);
					}
				}
			}
		}
		// Borderless is a window the size of the screen with no frame; only fullscreen takes the monitor over.
		m_Window = glfwCreateWindow(width, height, windowTitle.c_str(), project.Mode == WindowMode::Fullscreen && fromProject ? monitor : nullptr, nullptr);

		glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
			{
				Engine::get().request_quit();
			});
	}

	void Window::shutdown()
	{
		glfwDestroyWindow(m_Window);
		glfwTerminate();
	}

	void Window::post_render_update(float deltaTime)
	{
		glfwPollEvents();
	}
	void Window::set_title(const std::string& newTitle)
	{
		glfwSetWindowTitle(m_Window, newTitle.c_str());
	}
	void Window::get_size(int& width, int& height)
	{
		glfwGetFramebufferSize(m_Window, &width, &height);
	}
}