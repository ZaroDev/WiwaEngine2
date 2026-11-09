#include <pch.h>
#include "render_view.h"

#include "../render_module.h"

namespace wiwa
{
	RenderView::~RenderView()
	{
		reset();
	}

	RenderView::RenderView(RenderView&& other) noexcept
		: m_Module(other.m_Module), m_Alive(std::move(other.m_Alive)), m_Id(other.m_Id)
	{
		other.m_Module = nullptr;
		other.m_Id = INVALID_VIEW;
	}

	RenderView& RenderView::operator=(RenderView&& other) noexcept
	{
		if (this != &other)
		{
			reset();
			m_Module = other.m_Module;
			m_Alive = std::move(other.m_Alive);
			m_Id = other.m_Id;
			other.m_Module = nullptr;
			other.m_Id = INVALID_VIEW;
		}
		return *this;
	}

	RenderModule* RenderView::live_module() const
	{
		return (m_Id != INVALID_VIEW && !m_Alive.expired()) ? m_Module : nullptr;
	}

	void RenderView::reset()
	{
		if (RenderModule* module = live_module())
		{
			module->view_destroy(m_Id);
		}
		m_Module = nullptr;
		m_Id = INVALID_VIEW;
		m_Alive.reset();
	}

	void RenderView::set_scene(const Ref<Scene>& scene)
	{
		if (RenderModule* module = live_module())
		{
			module->view_set_scene(m_Id, scene);
		}
	}

	void RenderView::set_camera(const RenderCamera& camera)
	{
		if (RenderModule* module = live_module())
		{
			module->view_set_camera(m_Id, camera);
		}
	}

	void RenderView::resize(u32 width, u32 height)
	{
		if (RenderModule* module = live_module())
		{
			module->view_resize(m_Id, width, height);
		}
	}

	void RenderView::request_render()
	{
		if (RenderModule* module = live_module())
		{
			module->view_request_render(m_Id);
		}
	}

	bool RenderView::failed() const
	{
		RenderModule* module = live_module();
		return module != nullptr && module->view_failed(m_Id);
	}

	void RenderView::read_back(std::function<void(ImageData)> done, bool premultiplied)
	{
		if (RenderModule* module = live_module())
		{
			module->view_read_back(m_Id, std::move(done), premultiplied);
		}
		else if (done)
		{
			done(ImageData{});
		}
	}
}
