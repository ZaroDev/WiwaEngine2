#pragma once

#include <filesystem>
#include <mutex>
#include <vector>

#include <core/module.h>

struct GLFWwindow;
namespace wiwa
{
	class Window final : public IModule
	{
	public:
		Window() = default;
		virtual ~Window() override = default;

		virtual void init() override;
		virtual void shutdown() override;

		virtual void post_render_update(float deltaTime) override;

		virtual UpdateFlags get_update_flags() const override { return UpdateFlags::UPDATE_RENDERER; }
		virtual std::string get_name() const { return "Window"; }

		virtual int get_priority() const { return INT_MAX; }

		void set_title(const std::string& newTitle);

		GLFWwindow* get_window() const { return m_Window; }

		void get_size(int& width, int& height);

		// The files dragged onto the window from the OS since the last call (the editor imports them; the game ignores them).
		std::vector<std::filesystem::path> take_dropped_files();
	private:
		std::mutex m_DropMutex{};
		std::vector<std::filesystem::path> m_DroppedFiles{};
		GLFWwindow* m_Window = nullptr;
	};
}