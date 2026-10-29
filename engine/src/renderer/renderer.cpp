#include <pch.h>
#include "renderer.h"

#include <algorithm>

#include <nvrhi/utils.h>

#include "shader_library.h"

#define WI_RENDER_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	// A present that keeps asking for a resize at the size we already have is ignored after this many frames in a row.
	constexpr u32 MAX_PRESENT_RESIZE_STREAK = 2;

	Renderer::Renderer() = default;

	Renderer::~Renderer()
	{
		shutdown();
	}

	bool Renderer::initialize(const RendererDescription& description)
	{
		m_Description = description;
		if (m_Description.Window == nullptr)
		{
			WI_RENDER_LOG(Error, "RendererDescription::Window is null");
			return false;
		}

		m_Description.MaxFramesInFlight = std::clamp(m_Description.MaxFramesInFlight, 1u, 8u);
		m_RequestedVsync = m_Description.EnableVsync;

		m_Backend = create_render_backend(m_Description.Api);
		if (m_Backend == nullptr)
		{
			return false;
		}

		if (!m_Backend->initialize(m_Description, std::max(m_Description.Width, 1u), std::max(m_Description.Height, 1u)))
		{
			m_Backend.reset();
			return false;
		}

		m_CommandList = m_Backend->get_device()->createCommandList();

		if (m_Description.Assets == nullptr)
		{
			WI_RENDER_LOG(Error, "RendererDescription::Assets is null");
			m_Backend->shutdown();
			m_Backend.reset();
			return false;
		}
		m_Shaders = CreateScope<ShaderLibrary>(m_Backend->get_device(), m_Description.Api, m_Description.Assets);

		WI_RENDER_LOG(Info, "Using {0} on {1}", graphics_api_name(m_Description.Api), m_Backend->get_adapter_name());
		m_Initialized = true;
		return true;
	}

	void Renderer::shutdown()
	{
		if (m_Backend == nullptr)
		{
			return;
		}

		if (nvrhi::IDevice* device = m_Backend->get_device())
		{
			device->waitForIdle();
		}

		m_Passes.clear();
		m_Shaders.reset();
		m_Profiler = PassProfiler{}; // its timer queries belong to the device
		m_CommandList = nullptr;
		m_FramesInFlight.clear();
		m_QueryPool.clear();

		m_Backend->shutdown();
		m_Backend.reset();
		m_Initialized = false;
	}

	bool Renderer::add_pass(Scope<IRenderPass> pass)
	{
		if (!m_Initialized || m_Backend->get_reference_framebuffer() == nullptr)
		{
			WI_RENDER_LOG(Error, "add_pass called before initialize");
			return false;
		}

		if (!pass->initialize(make_pass_description()))
		{
			return false;
		}

		m_Passes.push_back(std::move(pass));
		return true;
	}

	bool Renderer::render_frame(const RenderBatch& batch)
	{
		if (batch.Minimized || batch.Width == 0 || batch.Height == 0)
		{
			return true; // nothing to show; resumes with the first normal batch
		}

		m_Time += batch.DeltaTime;

		if (batch.Width != m_Backend->get_width() || batch.Height != m_Backend->get_height())
		{
			return resize_backend(batch.Width, batch.Height);
		}

		if (m_RequestedVsync != m_Description.EnableVsync)
		{
			return apply_vsync(batch.Width, batch.Height);
		}

		nvrhi::IDevice* device = m_Backend->get_device();

		m_Shaders->update();

		// Finish asynchronous work (pipeline creation, shaders that became ready) and check whether every pass can render yet.
		bool allPassesReady = true;
		for (const Scope<IRenderPass>& pass : m_Passes)
		{
			pass->update(device, m_Backend->get_reference_framebuffer());
			if (pass->has_failed())
			{
				WI_RENDER_LOG(Error, "Pass {0} failed", pass->get_name());
				return false;
			}
			allPassesReady = allPassesReady && pass->is_ready();
		}
		m_PassesReady = allPassesReady;

		// Throttle the CPU before touching any per-frame state: after this there is room for one more frame.
		wait_for_frame_slot();

		nvrhi::IFramebuffer* backbuffer = nullptr;
		FrameStatus status = m_Backend->acquire_frame(backbuffer);
		if (status == FrameStatus::ResizeNeeded)
		{
			return resize_backend(batch.Width, batch.Height);
		}
		if (status == FrameStatus::Failed)
		{
			return false;
		}

		m_CommandList->open();

		// Forces the next state to be applied from scratch. D3D11's flip-model swapchain
		// unbinds the backbuffer on Present, so cached bindings can't be trusted across frames.
		m_CommandList->clearState();

		// The passes draw over this; until they are ready (or when there are none) it is what the window shows.
		nvrhi::utils::ClearColorAttachment(m_CommandList, backbuffer, 0, nvrhi::Color(0.02f, 0.02f, 0.05f, 1.0f));

		if (allPassesReady && !m_Passes.empty())
		{
			if (!m_Profiler.is_initialized_for(m_Passes.size()))
			{
				std::vector<const char*> names;
				for (const Scope<IRenderPass>& pass : m_Passes)
				{
					names.push_back(pass->get_name());
				}
				m_Profiler.initialize(device, std::move(names), m_Description.MaxFramesInFlight + 1);
			}

			const FrameDescription frame = make_frame_description(backbuffer, batch);
			m_Profiler.begin_frame(device, frame.FrameIndex);
			for (size_t i = 0; i < m_Passes.size(); i++)
			{
				m_Profiler.begin_pass(m_CommandList, i);
				m_Passes[i]->render(m_CommandList, frame);
				m_Profiler.end_pass(m_CommandList, i);
			}
			m_Profiler.end_frame(m_Time);
		}

		m_CommandList->close();

		status = m_Backend->submit_and_present(m_CommandList);
		if (status != FrameStatus::Failed)
		{
			mark_frame_submitted(); // also true for ResizeNeeded: the frame was submitted and presented
			m_FrameIndex++;
		}

		if (status == FrameStatus::ResizeNeeded)
		{
			// The size matches the window, so the swapchain is only "suboptimal". Some drivers say so on every present
			// forever; rebuild it a couple of times, then live with it.
			if (m_PresentResizeStreak >= MAX_PRESENT_RESIZE_STREAK)
			{
				return true;
			}
			m_PresentResizeStreak++;
			return resize_backend(batch.Width, batch.Height);
		}

		m_PresentResizeStreak = 0;
		return status == FrameStatus::Ok;
	}

	PassInitializationDescription Renderer::make_pass_description() const
	{
		PassInitializationDescription description;
		description.Device = m_Backend->get_device();
		description.Backbuffer = m_Backend->get_reference_framebuffer();
		description.Shaders = m_Shaders.get();
		return description;
	}

	FrameDescription Renderer::make_frame_description(nvrhi::IFramebuffer* backbuffer, const RenderBatch& batch)
	{
		FrameDescription frame;
		frame.Backbuffer = backbuffer;
		frame.Width = m_Backend->get_width();
		frame.Height = m_Backend->get_height();
		frame.Time = m_Time;
		frame.FrameIndex = m_FrameIndex;
		frame.ImGui = &batch.ImGui;
		return frame;
	}

	bool Renderer::apply_vsync(u32 width, u32 height)
	{
		m_Description.EnableVsync = m_RequestedVsync;
		if (!m_Backend->set_vsync(m_RequestedVsync))
		{
			return true; // applies from the next present
		}

		// The present mode is part of the swapchain: rebuild it at the same size (the passes keep their targets).
		return m_Backend->resize(width, height);
	}

	bool Renderer::resize_backend(u32 width, u32 height)
	{
		if (!m_Backend->resize(width, height))
		{
			WI_RENDER_LOG(Error, "Resizing the swapchain to {0}x{1} failed", width, height);
			return false;
		}

		for (const Scope<IRenderPass>& pass : m_Passes)
		{
			pass->on_resize(m_Backend->get_device(), m_Backend->get_width(), m_Backend->get_height());
		}

		return true;
	}

	void Renderer::wait_for_frame_slot()
	{
		nvrhi::IDevice* device = m_Backend->get_device();

		while (m_FramesInFlight.size() >= m_Description.MaxFramesInFlight)
		{
			nvrhi::EventQueryHandle query = std::move(m_FramesInFlight.front());
			m_FramesInFlight.pop_front();

			device->waitEventQuery(query);
			m_QueryPool.push_back(std::move(query));
		}
	}

	void Renderer::mark_frame_submitted()
	{
		nvrhi::IDevice* device = m_Backend->get_device();

		nvrhi::EventQueryHandle query;
		if (!m_QueryPool.empty())
		{
			query = std::move(m_QueryPool.back());
			m_QueryPool.pop_back();
		}
		else
		{
			query = device->createEventQuery();
		}

		device->resetEventQuery(query);
		device->setEventQuery(query, nvrhi::CommandQueue::Graphics);
		m_FramesInFlight.push_back(std::move(query));
	}
}
