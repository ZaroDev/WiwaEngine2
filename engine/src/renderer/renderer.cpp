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

	// How much the GPU resource cache may upload in one frame.
	constexpr size_t UPLOAD_BYTES_PER_FRAME = 16 * 1024 * 1024;

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

		if (!m_Uploads.initialize(m_Backend->get_device(), m_Backend->has_copy_queue(), UPLOAD_BYTES_PER_FRAME))
		{
			m_Backend->shutdown();
			m_Backend.reset();
			return false;
		}
		if (!m_Description.EnableCopyQueue)
		{
			m_Uploads.force_graphics_queue(true);
		}
		m_Cache = CreateScope<GpuResourceCache>(m_Backend->get_device(), m_Description.Assets, &m_Uploads);
		m_Jobs = CreateScope<JobSystem>(2);
		m_Environments = CreateScope<EnvironmentCache>(m_Backend->get_device(), m_Description.Assets, &m_Uploads, m_Jobs.get());
		m_SceneData.set_environments(m_Environments.get());
		if (!m_SceneData.initialize(m_Backend->get_device()))
		{
			WI_RENDER_LOG(Error, "Could not create the scene render data");
			m_Backend->shutdown();
			m_Backend.reset();
			return false;
		}

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
		m_SceneData.shutdown();
		m_Textures = TextureRegistry{};
		m_Viewport.destroy();
		m_Environments.reset();
		m_Jobs.reset();
		m_Cache.reset();
		m_Uploads.shutdown();
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

		// The uploads go out first, as a list of their own that is closed before the frame list opens (D3D11 has one immediate
		// context: two lists cannot be open at once). The graphics queue waits for the copy queue before the frame list runs.
		m_Uploads.begin_frame();
		m_Cache->update();
		m_Environments->update();
		m_Uploads.submit(m_Backend->get_last_graphics_instance());

		update_viewport();
		m_SceneData.build(m_Scene, *m_Cache, m_Viewport);

		m_CommandList->open();
		m_SceneData.write(m_CommandList, *m_Cache);

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

	PassInitializationDescription Renderer::make_pass_description()
	{
		PassInitializationDescription description;
		description.Device = m_Backend->get_device();
		description.Backbuffer = m_Backend->get_reference_framebuffer();
		description.Shaders = m_Shaders.get();
		description.Textures = &m_Textures;
		return description;
	}

	FrameDescription Renderer::make_frame_description(nvrhi::IFramebuffer* backbuffer, const RenderBatch& batch)
	{
		FrameDescription frame;
		frame.Backbuffer = backbuffer;
		frame.Width = m_Backend->get_width();
		frame.Height = m_Backend->get_height();
		frame.Time = m_Time;
		frame.DeltaTime = batch.DeltaTime;
		frame.FrameIndex = m_FrameIndex;
		frame.ImGui = &batch.ImGui;
		frame.Debug = &batch.Debug;
		frame.Scene = &m_SceneData;
		frame.SceneInViewport = m_SceneInViewport;
		return frame;
	}

	void Renderer::set_quality(const RenderQuality& quality)
	{
		m_Quality = sanitized(quality);
		m_SceneData.quality() = m_Quality;
		m_RequestedVsync = m_Quality.Vsync;
		m_Uploads.force_graphics_queue(!m_Quality.CopyQueue || !m_Description.EnableCopyQueue);
	}

	void Renderer::set_viewport_size(u32 width, u32 height)
	{
		if (width == 0 || height == 0)
		{
			return;
		}
		if (width != m_RequestedViewportWidth || height != m_RequestedViewportHeight)
		{
			m_ViewportFailed = false;
		}
		m_RequestedViewportWidth = width;
		m_RequestedViewportHeight = height;
	}

	void Renderer::update_viewport()
	{
		// Without a panel the scene is the window: the target follows the swapchain.
		const u32 requestedWidth = m_SceneInViewport ? m_RequestedViewportWidth : m_Backend->get_width();
		const u32 requestedHeight = m_SceneInViewport ? m_RequestedViewportHeight : m_Backend->get_height();
		if (requestedWidth == 0 || requestedHeight == 0 || (m_SceneInViewport && m_ViewportFailed))
		{
			return;
		}

		const u32 width = ViewportTarget::clamp_dimension(requestedWidth);
		const u32 height = ViewportTarget::clamp_dimension(requestedHeight);
		if (m_Viewport.valid() && m_Viewport.width() == width && m_Viewport.height() == height)
		{
			return;
		}

		// create() lets go of the old targets at once. The frames still in flight may use them, and NVRHI's Vulkan backend does not
		// keep a texture alive that a command list only clears, so the GPU has to be done with them first.
		if (m_Viewport.valid())
		{
			m_Backend->get_device()->waitForIdle();
		}

		if (m_Viewport.create(m_Backend->get_device(), width, height))
		{
			m_Textures.set(VIEWPORT_TEXTURE_ID, m_Viewport.ldr());
		}
		else
		{
			m_ViewportFailed = true;
			m_Textures.set(VIEWPORT_TEXTURE_ID, nullptr);
		}
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
