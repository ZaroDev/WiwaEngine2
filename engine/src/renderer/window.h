#pragma once

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
	private:
		GLFWwindow* m_Window = nullptr;
	};
}