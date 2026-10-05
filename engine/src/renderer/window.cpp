#include <pch.h>
#include "window.h"

#include <glad/glad.h>
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
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);

		std::string windowTitle = fmt::format("Wiwa 2 <{0}>", WI_BUILD_CONFIGURATION);
		m_Window = glfwCreateWindow(1280, 720, windowTitle.c_str(), nullptr, nullptr);


		glfwMakeContextCurrent(m_Window);
		if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
		{
			return;
		}
		glfwSwapInterval(false);

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
		glfwSwapBuffers(m_Window);
	}
	void Window::set_title(const std::string& newTitle)
	{
		glfwSetWindowTitle(m_Window, newTitle.c_str());
	}
}