#include <pch.h>
#include "renderer.h"

#include <algorithm>
#include <cstring>

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
		m_Cache = CreateScope<GpuResourceCache>(m_Backend->get_device(), m_Description.Assets, &m_Uploads, m_Description.Api);
		m_Jobs = CreateScope<JobSystem>(2);
		m_Environments = CreateScope<EnvironmentCache>(m_Backend->get_device(), m_Description.Assets, &m_Uploads, m_Jobs.get());
		m_MainView.Id = MAIN_VIEW;
		m_MainView.Flags = view_flags_for(ViewProfile::Full);
		m_MainView.Data.set_environments(m_Environments.get());
		if (!m_MainView.Data.initialize(m_Backend->get_device()))
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
		auto make_stand_in = [&](const char* name)
		{
			nvrhi::TextureDesc description;
			description.width = 1;
			description.height = 1;
			description.format = nvrhi::Format::RGBA8_UNORM;
			description.isShaderResource = true;
			description.initialState = nvrhi::ResourceStates::ShaderResource;
			description.keepInitialState = true;
			description.debugName = name;
			return m_Backend->get_device()->createTexture(description);
		};
		m_ViewBlank = make_stand_in("View stand-in (not drawn yet)");
		m_ViewFailed = make_stand_in("View stand-in (failed)");

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
		m_ReadbackCopies.clear();
		m_ReadbackQueue.clear();
		m_ViewBlank = nullptr;
		m_ViewFailed = nullptr;
		for (const Scope<ViewState>& view : m_ExtraViews)
		{
			view->Data.shutdown();
			view->Target.destroy();
		}
		m_ExtraViews.clear();
		m_MainView.Data.shutdown();
		m_Textures = TextureRegistry{};
		m_MainView.Target.destroy();
		m_Environments.reset();
		m_Jobs.reset();
		m_ParticlePool.shutdown();
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

		m_Time += batch.DeltaSeconds;

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
		if (status == FrameStatus::Failed)
		{
			// The copies recorded in this frame never ran: their staging images hold nothing.
			drop_unsubmitted_readbacks();
		}

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
		const std::vector<ViewState*> views = pick_views_to_render();
		for (ViewState* view : views)
		{
			if (!view->is_main())
			{
				update_view_target(*view);
			}
			view->Data.quality() = effective_quality(*view);
			view->Data.set_view_flags(view->Flags);
			bool transparent = view->Desc.TransparentBackground && !view->is_main();
			if (transparent && view->Data.quality().Taa)
			{
				// The history of TAA mixes alpha with color: not supported.
				if (!view->WarnedTaaAlpha)
				{
					WI_RENDER_LOG(Warning, "View {0}: a transparent background is not supported with TAA; the background is drawn", view->Id);
					view->WarnedTaaAlpha = true;
				}
				transparent = false;
			}
			view->Data.set_transparent_background(transparent);
			view->Data.set_frame_index(m_FrameIndex);
			view->Data.build(view->Scene, *m_Cache, view->Target);
		}

		deliver_finished_readbacks();

		m_CommandList->open();
		write_view_stand_ins();
		for (ViewState* view : views)
		{
			view->Data.write(m_CommandList, *m_Cache);
		}

		// Forces the next state to be applied from scratch. D3D11's flip-model swapchain
		// unbinds the backbuffer on Present, so cached bindings can't be trusted across frames.
		m_CommandList->clearState();
		std::vector<FrameDescription> frames; // filled when the passes run: they hear about the submitted frame
		frames.reserve(views.size());

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

			m_Profiler.begin_frame(device, m_FrameIndex);
			// The offscreen views come first (the list ends with the main view), so what ImGui and the game UI sample is finished.
			for (ViewState* view : views)
			{
				frames.push_back(make_frame_description(backbuffer, batch, *view));
				const FrameDescription& frame = frames.back();
				for (size_t i = 0; i < m_Passes.size(); i++)
				{
					if (!m_Passes[i]->runs_in_view(view->Flags, view->is_main()))
					{
						continue;
					}
					if (view->is_main())
					{
						m_Profiler.begin_pass(m_CommandList, i);
					}
					m_Passes[i]->render(m_CommandList, frame);
					if (view->is_main())
					{
						m_Profiler.end_pass(m_CommandList, i);
					}
				}
				view->Scene.end_frame(); // the next frame's velocity is the motion since this one
				view->LastRenderedFrame = m_FrameIndex;
				view->EverRendered = true;
				view->RenderRequested = false;
			}
			m_Profiler.end_frame(m_Time);
			record_readbacks(views);
		}

		m_CommandList->close();

		status = m_Backend->submit_and_present(m_CommandList);
		if (status != FrameStatus::Failed)
		{
			mark_frame_submitted(); // also true for ResizeNeeded: the frame was submitted and presented
			mark_readbacks_submitted();
			m_FrameIndex++;
			for (const FrameDescription& frame : frames)
			{
				for (const Scope<IRenderPass>& pass : m_Passes)
				{
					if (pass->runs_in_view(frame.View->Flags, frame.IsMainView))
					{
						pass->on_frame_submitted(device, frame);
					}
				}
			}
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
		description.Cache = m_Cache.get();
		description.Particles = &m_ParticlePool;
		return description;
	}

	FrameDescription Renderer::make_frame_description(nvrhi::IFramebuffer* backbuffer, const RenderBatch& batch, ViewState& view)
	{
		FrameDescription frame;
		frame.Backbuffer = backbuffer;
		frame.Width = m_Backend->get_width();
		frame.Height = m_Backend->get_height();
		frame.Time = m_Time;
		frame.DeltaSeconds = batch.DeltaSeconds;
		frame.FrameIndex = m_FrameIndex;
		frame.ImGui = &batch.ImGui;
		frame.Debug = &batch.Debug;
		frame.Ui = &batch.Ui;
		frame.Pick = &m_Pick;
		frame.Scene = &view.Data;
		frame.View = &view;
		frame.IsMainView = view.is_main();
		frame.SceneInViewport = m_SceneInViewport;
		return frame;
	}

	RenderQuality Renderer::effective_quality(const ViewState& view) const
	{
		RenderQuality quality = m_Quality;
		if (!view.is_main())
		{
			// The profile decides the anti-aliasing of an offscreen view: TAA only when the project has it on and the profile allows it, FXAA
			// otherwise when the profile allows it (a preview without any smoothing looks bad whatever the project says).
			quality.Taa = quality.Taa && view.Flags.Taa;
			quality.Fxaa = !quality.Taa && view.Flags.Fxaa;
		}
		return quality;
	}

	void Renderer::set_quality(const RenderQuality& quality)
	{
		const u32 previousPool = m_Quality.ParticlePoolSize;
		m_Quality = sanitized(quality);
		if (m_Quality.ParticlePoolSize != previousPool)
		{
			m_ParticlePool.set_capacity(m_Quality.ParticlePoolSize); // only when the project changed it: a pool made by hand (a test) is left alone
		}
		m_MainView.Data.quality() = m_Quality;
		for (const Scope<ViewState>& view : m_ExtraViews)
		{
			view->Data.quality() = effective_quality(*view);
		}
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
		if (m_MainView.Target.valid() && m_MainView.Target.width() == width && m_MainView.Target.height() == height)
		{
			return;
		}

		// create() lets go of the old targets at once. The frames still in flight may use them, and NVRHI's Vulkan backend does not
		// keep a texture alive that a command list only clears, so the GPU has to be done with them first.
		if (m_MainView.Target.valid())
		{
			m_Backend->get_device()->waitForIdle();
		}

		if (m_MainView.Target.create(m_Backend->get_device(), width, height))
		{
			m_Textures.set(VIEWPORT_TEXTURE_ID, m_MainView.Target.ldr());
		}
		else
		{
			m_ViewportFailed = true;
			m_Textures.set(VIEWPORT_TEXTURE_ID, nullptr);
		}
	}

	ViewState* Renderer::find_view(ViewId id)
	{
		if (id == MAIN_VIEW)
		{
			return &m_MainView;
		}
		for (const Scope<ViewState>& view : m_ExtraViews)
		{
			if (view->Id == id)
			{
				return view.get();
			}
		}
		return nullptr;
	}

	ViewId Renderer::add_view(ViewId id, const ViewDesc& desc)
	{
		if (!m_Initialized || id == MAIN_VIEW || id == INVALID_VIEW || find_view(id) != nullptr)
		{
			return INVALID_VIEW;
		}
		if (view_count() >= MAX_VIEWS)
		{
			WI_RENDER_LOG(Warning, "Cannot make view {0}: {1} views are live", id, view_count());
			return INVALID_VIEW;
		}

		Scope<ViewState> view = CreateScope<ViewState>();
		view->Id = id;
		view->Desc = desc;
		view->TextureId = view_texture_id(id);
		m_Textures.set(view->TextureId, m_ViewBlank);
		view->Flags = desc.flags();
		view->Data.set_environments(m_Environments.get());
		if (!view->Data.initialize(m_Backend->get_device(), &m_MainView.Data))
		{
			WI_RENDER_LOG(Error, "Could not create the scene render data of view {0}", id);
			return INVALID_VIEW;
		}
		view->Data.quality() = effective_quality(*view);
		view->Data.set_view_flags(view->Flags);
		view->RequestedWidth = desc.Width;
		view->RequestedHeight = desc.Height;
		m_ExtraViews.push_back(std::move(view));
		update_view_target(*m_ExtraViews.back());
		return id;
	}

	void Renderer::remove_view(ViewId id)
	{
		for (size_t i = 0; i < m_ExtraViews.size(); i++)
		{
			if (m_ExtraViews[i]->Id != id)
			{
				continue;
			}
			// The frames still in flight may be using the target.
			m_Backend->get_device()->waitForIdle();
			// The passes let go of what they cached for its target.
			for (const Scope<IRenderPass>& pass : m_Passes)
			{
				pass->on_view_removed(m_ExtraViews[i]->Target.uid());
			}
			// Nobody waits for the pixels of a view that is gone.
			m_ReadbackCopies.erase(std::remove_if(m_ReadbackCopies.begin(), m_ReadbackCopies.end(), [id](const ReadbackCopy& copy) { return copy.Request.View == id; }), m_ReadbackCopies.end());
			m_ReadbackQueue.erase(std::remove_if(m_ReadbackQueue.begin(), m_ReadbackQueue.end(), [id](const ReadbackRequest& request) { return request.View == id; }), m_ReadbackQueue.end());
			ViewState& view = *m_ExtraViews[i];
			m_Textures.remove(view.TextureId);
			view.Data.shutdown();
			view.Target.destroy();
			m_ExtraViews.erase(m_ExtraViews.begin() + static_cast<std::ptrdiff_t>(i));
			return;
		}
	}

	void Renderer::resize_view(ViewId id, u32 width, u32 height)
	{
		ViewState* view = find_view(id);
		if (view == nullptr || view->is_main() || width == 0 || height == 0)
		{
			return;
		}
		if (width != view->RequestedWidth || height != view->RequestedHeight)
		{
			view->Failed = false;
		}
		view->RequestedWidth = width;
		view->RequestedHeight = height;
	}

	void Renderer::update_view_target(ViewState& view)
	{
		u32 width = ViewportTarget::clamp_dimension(view.RequestedWidth);
		u32 height = ViewportTarget::clamp_dimension(view.RequestedHeight);
		const u32 cap = view.Flags.MaxDimension;
		if (cap > 0 && std::max(width, height) > cap)
		{
			// Keeps the aspect ratio.
			const float scale = static_cast<float>(cap) / static_cast<float>(std::max(width, height));
			width = std::max(1u, static_cast<u32>(static_cast<float>(width) * scale));
			height = std::max(1u, static_cast<u32>(static_cast<float>(height) * scale));
		}
		if (view.Failed || (view.Target.valid() && view.Target.width() == width && view.Target.height() == height))
		{
			return;
		}

		if (view.Target.valid())
		{
			m_Backend->get_device()->waitForIdle();
		}
		if (view.Target.create(m_Backend->get_device(), width, height))
		{
			m_Textures.set(view.TextureId, view.Target.ldr());
		}
		else
		{
			WI_RENDER_LOG(Error, "Could not make the target of view {0} at {1}x{2}", view.Id, width, height);
			view.Failed = true;
			m_Textures.set(view.TextureId, m_ViewFailed);
		}
	}

	std::vector<ViewState*> Renderer::pick_views_to_render()
	{
		std::vector<ViewState*> candidates;
		for (const Scope<ViewState>& view : m_ExtraViews)
		{
			const bool wanted = view->Desc.Mode == ViewMode::Continuous || view->RenderRequested;
			if (wanted && !view->Failed)
			{
				candidates.push_back(view.get());
			}
		}
		// Continuous views first, then one-shots; each group by the one that waited longest.
		std::sort(candidates.begin(), candidates.end(), [](const ViewState* a, const ViewState* b)
			{
				const bool aContinuous = a->Desc.Mode == ViewMode::Continuous;
				const bool bContinuous = b->Desc.Mode == ViewMode::Continuous;
				if (aContinuous != bContinuous)
				{
					return aContinuous;
				}
				return a->LastRenderedFrame < b->LastRenderedFrame;
			});
		if (candidates.size() > MAX_VIEWS_PER_FRAME)
		{
			candidates.resize(MAX_VIEWS_PER_FRAME);
		}
		candidates.push_back(&m_MainView);
		return candidates;
	}

	void Renderer::write_view_stand_ins()
	{
		if (m_StandInsWritten || m_ViewBlank == nullptr || m_ViewFailed == nullptr)
		{
			return;
		}
		m_StandInsWritten = true;
		const u8 blank[4] = { 0, 0, 0, 0 };
		const u8 magenta[4] = { 255, 0, 255, 255 };
		m_CommandList->writeTexture(m_ViewBlank, 0, 0, blank, 4);
		m_CommandList->writeTexture(m_ViewFailed, 0, 0, magenta, 4);
	}

	void Renderer::answer_empty(u64 requestId)
	{
		if (m_ReadbackChannel != nullptr)
		{
			m_ReadbackChannel->publish(requestId, ImageData{});
		}
	}

	void Renderer::request_readback(ViewId view, u64 requestId, bool premultiplied)
	{
		ViewState* state = find_view(view);
		if (state == nullptr || state->Failed)
		{
			answer_empty(requestId);
			return;
		}
		m_ReadbackQueue.push_back({ view, requestId, premultiplied, m_FrameIndex });
	}

	// Copies the image of every waiting request whose view can be read now. A view is readable when it drew this frame, or when it has drawn
	// and is not waiting to draw again (a OneShot view with nothing requested keeps its last image).
	void Renderer::record_readbacks(const std::vector<ViewState*>& rendered)
	{
		for (auto it = m_ReadbackQueue.begin(); it != m_ReadbackQueue.end() && m_ReadbackCopies.size() < MAX_READBACKS_IN_FLIGHT;)
		{
			ViewState* view = find_view(it->View);
			if (view == nullptr || view->Failed)
			{
				answer_empty(it->Id);
				it = m_ReadbackQueue.erase(it);
				continue;
			}
			const bool drewNow = std::find(rendered.begin(), rendered.end(), view) != rendered.end();
			const bool readable = drewNow || (view->EverRendered && !view->RenderRequested && view->Desc.Mode == ViewMode::OneShot);
			// A view that is never going to draw (a OneShot view nobody asked to render) or that waits too long for its turn: the caller gets an
			// empty answer (a failure) instead of silence.
			const bool neverDrawn = view->Desc.Mode == ViewMode::OneShot && !view->EverRendered && !view->RenderRequested;
			if (!readable && (neverDrawn || m_FrameIndex - it->QueuedFrame > MAX_READBACK_WAIT_FRAMES))
			{
				answer_empty(it->Id);
				it = m_ReadbackQueue.erase(it);
				continue;
			}
			if (!readable || !view->Target.valid())
			{
				++it;
				continue;
			}

			nvrhi::TextureDesc description;
			description.width = view->Target.width();
			description.height = view->Target.height();
			description.format = nvrhi::Format::RGBA8_UNORM;
			description.debugName = "View readback";
			ReadbackCopy copy;
			copy.Request = *it;
			copy.Transparent = view->Desc.TransparentBackground;
			copy.Width = description.width;
			copy.Height = description.height;
			copy.Staging = m_Backend->get_device()->createStagingTexture(description, nvrhi::CpuAccessMode::Read);
			if (copy.Staging == nullptr)
			{
				WI_RENDER_LOG(Error, "Could not make the staging image of a readback of view {0}", it->View);
				answer_empty(it->Id);
				it = m_ReadbackQueue.erase(it);
				continue;
			}
			m_CommandList->copyTexture(copy.Staging, nvrhi::TextureSlice(), view->Target.ldr(), nvrhi::TextureSlice());
			m_ReadbackCopies.push_back(std::move(copy));
			it = m_ReadbackQueue.erase(it);
		}
	}

	void Renderer::drop_unsubmitted_readbacks()
	{
		for (auto it = m_ReadbackCopies.begin(); it != m_ReadbackCopies.end();)
		{
			if (it->Submitted)
			{
				++it;
				continue;
			}
			answer_empty(it->Request.Id);
			it = m_ReadbackCopies.erase(it);
		}
	}

	// The copies recorded in the frame that was just submitted finish when this frame does.
	void Renderer::mark_readbacks_submitted()
	{
		nvrhi::IDevice* device = m_Backend->get_device();
		for (ReadbackCopy& copy : m_ReadbackCopies)
		{
			if (!copy.Submitted)
			{
				copy.Query = device->createEventQuery();
				device->setEventQuery(copy.Query, nvrhi::CommandQueue::Graphics);
				copy.Submitted = true;
			}
		}
	}

	void Renderer::deliver_finished_readbacks()
	{
		nvrhi::IDevice* device = m_Backend->get_device();
		for (auto it = m_ReadbackCopies.begin(); it != m_ReadbackCopies.end();)
		{
			if (!it->Submitted || !device->pollEventQuery(it->Query))
			{
				++it;
				continue;
			}

			ImageData image;
			size_t rowPitch = 0;
			const void* mapped = device->mapStagingTexture(it->Staging, nvrhi::TextureSlice(), nvrhi::CpuAccessMode::Read, &rowPitch);
			if (mapped != nullptr)
			{
				image.Width = it->Width;
				image.Height = it->Height;
				image.Premultiplied = it->Request.Premultiplied;
				image.Rgba.resize(static_cast<size_t>(it->Width) * it->Height * 4);
				const u8* source = static_cast<const u8*>(mapped);
				for (u32 y = 0; y < it->Height; y++)
				{
					u8* row = image.Rgba.data() + static_cast<size_t>(y) * it->Width * 4;
					std::memcpy(row, source + y * rowPitch, static_cast<size_t>(it->Width) * 4);
					if (it->Transparent && it->Request.Premultiplied)
					{
						// The target holds straight alpha (what ImGui and the UI blend with); the caller asked for premultiplied.
						for (u32 x = 0; x < it->Width; x++)
						{
							u8* pixel = row + x * 4;
							if (pixel[3] != 255)
							{
								for (int c = 0; c < 3; c++)
								{
									pixel[c] = static_cast<u8>((pixel[c] * pixel[3] + 127) / 255);
								}
							}
						}
					}
				}
				device->unmapStagingTexture(it->Staging);
			}
			else
			{
				WI_RENDER_LOG(Error, "Could not map the staging image of a readback");
			}
			if (m_ReadbackChannel != nullptr)
			{
				m_ReadbackChannel->publish(it->Request.Id, std::move(image));
			}
			it = m_ReadbackCopies.erase(it);
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
