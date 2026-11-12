#include <pch.h>
#include "render_module.h"

#include <GLFW/glfw3.h>

#include <animation/animation_module.h>
#include <assets/asset_manager.h>
#include <core/engine.h>
#include <scene/scene_module.h>

#include "passes/blit_pass.h"
#include "passes/debug_pass.h"
#include "passes/depth_prepass.h"
#include "passes/forward_pass.h"
#include "passes/imgui_pass.h"
#include "passes/ui_pass.h"
#include "video/video_texture.h"
#include "passes/decal_pass.h"
#include "passes/light_cull_pass.h"
#include "passes/particle_render_pass.h"
#include "passes/particle_sim_pass.h"
#include "passes/ao_pass.h"
#include "passes/bloom_pass.h"
#include "passes/exposure_pass.h"
#include "passes/pick_pass.h"
#include "passes/selection_outline_pass.h"
#include "passes/fxaa_pass.h"
#include "passes/motion_blur_pass.h"
#include "passes/taa_pass.h"
#include "passes/velocity_pass.h"
#include "passes/tonemap_pass.h"
#include "passes/shadow_pass.h"
#include "passes/sky_pass.h"
#include "shader_library.h"
#include "render_world.h"
#include "window.h"

#define WI_RENDER_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		RenderCheckRunner g_RenderCheckRunner{};
	}

	void set_render_check_runner(RenderCheckRunner runner)
	{
		g_RenderCheckRunner = std::move(runner);
	}
}

namespace
{
	constexpr const char* QUALITY_FILE = "render_quality.json";
}

namespace wiwa
{
	void RenderModule::init()
	{
		m_DebugDraw.set_enabled(false); // shapes are only collected while the renderer runs: until then nothing would send them
		if (!read_arguments())
		{
			return;
		}

		Window* window = Engine::get().get_module<Window>();
		m_Description.Window = window->get_window();
		std::string qualityError;
		if (const std::optional<RenderQuality> loaded = load_render_quality(QUALITY_FILE, qualityError))
		{
			m_Quality = *loaded;
		}
		else if (!qualityError.empty())
		{
			WI_RENDER_LOG(Warning, "{0}: the default quality settings are used", qualityError);
		}
		m_Description.EnableVsync = m_Quality.Vsync;
		m_Description.EnableCopyQueue = m_Description.EnableCopyQueue && m_Quality.CopyQueue;
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
				if (!m_Renderer->initialize(m_Description))
				{
					return false;
				}

				m_Renderer->set_pick_channel(&m_Picks);
				m_Renderer->set_readback_channel(&m_Readbacks);

				// The scene first (depth, shading, the 8-bit image); the UI goes over everything else, so its pass is the last one.
				Scope<UiRenderPass> uiPass = CreateScope<UiRenderPass>();
				m_UiPass = uiPass.get();
				Scope<ImGuiRenderPass> imguiPass = CreateScope<ImGuiRenderPass>();
				m_ImGuiPass = imguiPass.get();
				return m_Renderer->add_pass(CreateScope<ShadowPass>())
					&& m_Renderer->add_pass(CreateScope<DepthPrepass>())
					&& m_Renderer->add_pass(CreateScope<LightCullPass>())
					&& m_Renderer->add_pass(CreateScope<AoPass>())
					&& m_Renderer->add_pass(CreateScope<DecalPass>())
					&& m_Renderer->add_pass(CreateScope<ParticleSimPass>())
					&& m_Renderer->add_pass(CreateScope<ForwardPass>(ForwardPass::Stage::Opaque))
					&& m_Renderer->add_pass(CreateScope<SkyPass>())
					&& m_Renderer->add_pass(CreateScope<ForwardPass>(ForwardPass::Stage::Blended))
					&& m_Renderer->add_pass(CreateScope<ParticleRenderPass>())
					&& m_Renderer->add_pass(CreateScope<VelocityPass>())
					&& m_Renderer->add_pass(CreateScope<TaaPass>())
					&& m_Renderer->add_pass(CreateScope<MotionBlurPass>())
					&& m_Renderer->add_pass(CreateScope<ExposurePass>())
					&& m_Renderer->add_pass(CreateScope<BloomPass>())
					&& m_Renderer->add_pass(CreateScope<TonemapPass>())
					&& m_Renderer->add_pass(CreateScope<FxaaPass>())
					&& m_Renderer->add_pass(std::move(uiPass))
					&& m_Renderer->add_pass(CreateScope<SelectionOutlinePass>())
					&& m_Renderer->add_pass(CreateScope<DebugPass>())
					&& m_Renderer->add_pass(CreateScope<PickPass>())
					&& m_Renderer->add_pass(CreateScope<BlitPass>())
					&& m_Renderer->add_pass(std::move(imguiPass));
			},
			[this](RenderBatch& batch)
			{
				const auto frameStart = std::chrono::steady_clock::now();
				apply_reloads();

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

				run_render_test(world);

				publish_snapshot();

				if (m_LastFrameStart != std::chrono::steady_clock::time_point{})
				{
					using Millis = std::chrono::duration<float, std::milli>;
					std::lock_guard<std::mutex> lock(m_FrameSamplesMutex);
					if (m_FrameSamples.size() >= Series::Capacity)
					{
						m_FrameSamples.erase(m_FrameSamples.begin());
					}
					m_FrameSamples.emplace_back(Millis(frameStart - m_LastFrameStart).count(), Millis(std::chrono::steady_clock::now() - frameStart).count());
				}
				m_LastFrameStart = frameStart;
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
		m_DebugDraw.set_enabled(true);

		set_quality(m_Quality); // the render thread starts with the defaults: tell it what the file said

		// Hot reload: an asset cooked again is reported from the importer's thread; the render thread picks it up next frame.
		m_ReloadListenerId = m_Description.Assets->add_reload_listener([this](AssetGuid guid)
			{
				std::lock_guard<std::mutex> lock(m_ReloadMutex);
				m_ReloadedAssets.push_back(guid);
			});

		if (!m_ImGui.init(m_Description.Window))
		{
			fail_and_quit("Could not start the UI");
		}
	}

	void RenderModule::shutdown()
	{
		// Closes the queue, so a frame that is waiting to be submitted does not block the quit, and lets the render thread
		// finish its frame, wait for the GPU and destroy everything before the window goes away.
		if (m_ReloadListenerId != 0)
		{
			m_Description.Assets->remove_reload_listener(m_ReloadListenerId);
			m_ReloadListenerId = 0;
		}

		const bool wasStarted = m_Started;

		// The views die with the renderer: the handles that are still around go inert.
		m_ViewHosts.clear();
		m_Alive.reset();
		m_ViewRecords.clear();
		m_PendingReadbacks.clear();
		m_ViewNames = ViewRegistry{};

		m_Thread.stop();
		m_Renderer.reset();
		m_ImGuiPass = nullptr;
		m_UiPass = nullptr;
		m_ImGui.shutdown();
		m_Started = false;
		m_DebugDraw.set_enabled(false);

		if (m_SmokeFrames > 0 && wasStarted)
		{
			// Decided here, after the thread has stopped and the backend is gone: the last frames and the teardown count too.
			const u32 errors = get_error_message_count();
			if (errors == 0 && !m_Failed)
			{
				const RenderSnapshot last = m_Snapshot.read();
				WI_RENDER_LOG(Info, "Render smoke test passed ({0} frames on {1}; {2} instances, {3} draws, {4} failed assets)", m_SmokeCounted, graphics_api_name(m_Description.Api),
					last.SceneInstances, last.DrawCalls, last.CacheFailed);
			}
			else
			{
				WI_RENDER_LOG(Error, "Render smoke test FAILED ({0} GPU error message(s){1})", errors, m_Failed ? ", the renderer failed" : "");
				Engine::get().set_exit_code(1);
			}
		}

		WI_RENDER_LOG(Info, "Shutdown");
	}

	// Render thread: shaders that were cooked again are loaded again; the old ones stay in use until the new ones are ready.
	void RenderModule::apply_reloads()
	{
		std::vector<AssetGuid> reloaded;
		{
			std::lock_guard<std::mutex> lock(m_ReloadMutex);
			reloaded.swap(m_ReloadedAssets);
		}

		for (AssetGuid guid : reloaded)
		{
			m_Renderer->get_shader_library()->invalidate(guid);
			m_Renderer->get_cache().invalidate(guid);
			m_Renderer->get_environments().invalidate(guid);
		}
	}

	void RenderModule::set_vsync(bool enabled)
	{
		RenderQuality quality = m_Quality;
		quality.Vsync = enabled;
		set_quality(quality);
	}

	void RenderModule::set_quality(const RenderQuality& wanted)
	{
		const RenderQuality quality = sanitized(wanted);
		const bool changed = !(quality == m_Quality);
		m_Quality = quality;
		submit([quality](RenderWorld& world)
			{
				world.get_renderer().set_quality(quality);
			});
		if (changed && !save_render_quality(QUALITY_FILE, quality))
		{
			WI_RENDER_LOG(Warning, "Could not write {0}", QUALITY_FILE);
		}
	}

	void RenderModule::reset_temporal_history()
	{
		submit([](RenderWorld& world)
			{
				world.get_renderer().get_scene_data().request_history_reset();
			});
	}

	void RenderModule::set_debug_velocity(bool enabled)
	{
		submit([enabled](RenderWorld& world)
			{
				world.get_renderer().get_scene_data().set_debug_velocity(enabled);
			});
	}

	u32 RenderModule::get_asset_texture_id(const AssetRef& texture)
	{
		if (!texture.valid())
		{
			return 0;
		}
		const auto [it, added] = m_AssetTextureIds.try_emplace(texture.resolved(), m_NextAssetTextureId);
		if (added)
		{
			++m_NextAssetTextureId;
		}
		const u32 id = it->second;
		{
			std::lock_guard lock(m_AssetTextureMutex);
			if (m_AssetTexturesReady.contains(id))
			{
				return id;
			}
		}
		// Not in the registry yet: the render thread asks the cache (which starts the load) and registers the texture once uploaded.
		submit([this, texture, id](RenderWorld& world)
			{
				nvrhi::ITexture* gpu = world.get_cache().get_texture(texture);
				if (gpu == nullptr)
				{
					return;
				}
				world.get_renderer().get_textures().set(id, gpu);
				std::lock_guard lock(m_AssetTextureMutex);
				m_AssetTexturesReady.insert(id);
			});
		return 0;
	}

	u32 RenderModule::create_ui_image(const ImageData& image)
	{
		if (image.Width == 0 || image.Height == 0 || image.Rgba.size() < static_cast<size_t>(image.Width) * image.Height * 4)
		{
			return 0;
		}
		const u32 id = m_NextUiImageId++;
		auto pixels = std::make_shared<ImageData>(image);
		submit([pixels, id](RenderWorld& world)
			{
				nvrhi::IDevice* device = world.get_renderer().get_device();
				if (device == nullptr)
				{
					return;
				}
				nvrhi::TextureDesc description;
				description.width = pixels->Width;
				description.height = pixels->Height;
				description.format = nvrhi::Format::RGBA8_UNORM;
				description.initialState = nvrhi::ResourceStates::ShaderResource;
				description.keepInitialState = true;
				description.debugName = "UI image";
				nvrhi::TextureHandle texture = device->createTexture(description);
				if (!texture)
				{
					return;
				}
				nvrhi::CommandListHandle upload = device->createCommandList();
				upload->open();
				upload->writeTexture(texture, 0, 0, pixels->Rgba.data(), static_cast<size_t>(pixels->Width) * 4);
				upload->close();
				device->executeCommandList(upload);
				world.get_renderer().get_textures().set(id, texture);
			});
		return id;
	}

	void RenderModule::upload_video_frame(u32 stream, std::shared_ptr<const VideoFrame> frame)
	{
		if (!m_Started || !frame)
		{
			return;
		}
		UiRenderPass* pass = m_UiPass;
		submit([stream, pass, frame = std::move(frame)](RenderWorld& world)
			{
				upload_video_picture(world, stream, *frame, pass);
			});
	}

	void RenderModule::release_video_stream(u32 stream)
	{
		if (!m_Started)
		{
			return;
		}
		UiRenderPass* pass = m_UiPass;
		submit([stream, pass](RenderWorld& world)
			{
				release_video_picture(world, stream, pass);
			});
	}

	void RenderModule::release_ui_image(u32 id)
	{
		if (id < FIRST_UI_IMAGE_ID || id >= FIRST_VIEW_TEXTURE_ID)
		{
			return;
		}
		submit([this, id](RenderWorld& world)
			{
				nvrhi::ITexture* texture = world.get_renderer().get_textures().get(id);
				if (texture != nullptr && m_ImGuiPass != nullptr)
				{
					m_ImGuiPass->forget_texture(texture);
				}
				world.get_renderer().get_textures().set(id, nullptr);
			});
	}

	void RenderModule::set_viewport_size(u32 width, u32 height)
	{
		if (width == m_RequestedViewportWidth && height == m_RequestedViewportHeight)
		{
			return; // the render thread already knows
		}
		m_RequestedViewportWidth = width;
		m_RequestedViewportHeight = height;
		submit([width, height](RenderWorld& world)
			{
				world.get_renderer().set_viewport_size(width, height);
			});
	}

	ViewRecord* RenderModule::find_view_record(ViewId id)
	{
		for (const Scope<ViewRecord>& record : m_ViewRecords)
		{
			if (record->Id == id)
			{
				return record.get();
			}
		}
		return nullptr;
	}

	RenderView RenderModule::create_view(const ViewDesc& desc)
	{
		if (!m_Started)
		{
			return {};
		}
		if (m_ViewRecords.size() + 1 >= MAX_VIEWS)
		{
			if (!m_WarnedViewLimit)
			{
				WI_RENDER_LOG(Warning, "Cannot make another view: {0} views are live", MAX_VIEWS);
				m_WarnedViewLimit = true;
			}
			return {};
		}

		Scope<ViewRecord> record = CreateScope<ViewRecord>();
		record->Id = m_NextViewId++;
		record->Desc = desc;
		record->Width = std::max(desc.Width, 1u);
		record->Height = std::max(desc.Height, 1u);
		record->Desc.Width = record->Width;
		record->Desc.Height = record->Height;
		const ViewId id = record->Id;
		const ViewDesc sent = record->Desc;
		m_ViewRecords.push_back(std::move(record));
		submit([id, sent](RenderWorld& world)
			{
				world.get_renderer().add_view(id, sent);
			});
		return RenderView(this, m_Alive, id);
	}

	void RenderModule::view_destroy(ViewId id)
	{
		for (size_t i = 0; i < m_ViewRecords.size(); i++)
		{
			if (m_ViewRecords[i]->Id == id)
			{
				m_ViewRecords.erase(m_ViewRecords.begin() + static_cast<std::ptrdiff_t>(i));
				m_ViewNames.remove(id);
				m_WarnedViewLimit = false;
				for (auto pending = m_PendingReadbacks.begin(); pending != m_PendingReadbacks.end();)
				{
					pending = pending->second.View == id ? m_PendingReadbacks.erase(pending) : std::next(pending);
				}
				submit([id](RenderWorld& world)
					{
						world.get_renderer().remove_view(id);
					});
				return;
			}
		}
	}

	void RenderModule::view_set_scene(ViewId id, const Ref<Scene>& scene)
	{
		if (ViewRecord* record = find_view_record(id))
		{
			record->Scene = scene;
			record->HadScene = scene != nullptr;
			record->WarnedGone = false;
		}
	}

	void RenderModule::view_set_camera(ViewId id, const RenderCamera& camera)
	{
		if (ViewRecord* record = find_view_record(id))
		{
			record->Camera = camera;
			record->CameraDirty = true;
		}
	}

	void RenderModule::view_resize(ViewId id, u32 width, u32 height)
	{
		ViewRecord* record = find_view_record(id);
		if (record == nullptr || width == 0 || height == 0 || (width == record->Width && height == record->Height))
		{
			return;
		}
		record->Width = width;
		record->Height = height;
		submit([id, width, height](RenderWorld& world)
			{
				world.get_renderer().resize_view(id, width, height);
			});
	}

	void RenderModule::view_request_render(ViewId id)
	{
		ViewRecord* record = find_view_record(id);
		if (record == nullptr || record->Desc.Mode != ViewMode::OneShot)
		{
			return;
		}
		record->RenderPending = true;
		submit([id](RenderWorld& world)
			{
				if (ViewState* view = world.get_view(id))
				{
					view->RenderRequested = true;
				}
			});
	}

	void RenderModule::view_read_back(ViewId id, std::function<void(ImageData)> done, bool premultiplied)
	{
		if (find_view_record(id) == nullptr)
		{
			if (done)
			{
				done(ImageData{});
			}
			return;
		}
		const u64 requestId = m_NextReadbackId++;
		m_PendingReadbacks[requestId] = { id, std::move(done) };
		submit([id, requestId, premultiplied](RenderWorld& world)
			{
				world.get_renderer().request_readback(id, requestId, premultiplied);
			});
	}

	bool RenderModule::view_failed(ViewId id) const
	{
		return std::find(m_FailedViews.begin(), m_FailedViews.end(), id) != m_FailedViews.end();
	}

	// --render-smoke starts counting frames when there is something to render: at once without --scene, else once the scene has loaded
	// (a failed load is a failed run) and its assets are on the GPU (or a generous wait is over).
	bool RenderModule::smoke_ready()
	{
		if (!m_SmokeHasScene)
		{
			return true;
		}

		SceneModule* scenes = Engine::get().get_module<SceneModule>();
		const Ref<JobProgress> load = scenes != nullptr ? scenes->get_startup_load() : nullptr;
		if (load == nullptr || !load->finished())
		{
			return false;
		}
		if (!load->succeeded())
		{
			fail_and_quit("The scene given with --scene could not be loaded");
			return false;
		}

		constexpr u64 MAX_ASSET_WAIT_FRAMES = 900;
		if (m_Snapshot.read().CachePending > 0 && ++m_SmokeAssetWait < MAX_ASSET_WAIT_FRAMES)
		{
			return false;
		}
		return true;
	}

	void RenderModule::set_scene_in_viewport(bool inViewport)
	{
		if (inViewport == m_SentSceneInViewport)
		{
			return;
		}
		m_SentSceneInViewport = inViewport;
		submit([inViewport](RenderWorld& world)
			{
				world.get_renderer().set_scene_in_viewport(inViewport);
			});
	}

	void RenderModule::set_camera_override(const std::optional<RenderCamera>& camera)
	{
		const bool same = camera.has_value() == m_SentCameraOverride.has_value()
			&& (!camera || std::memcmp(&*camera, &*m_SentCameraOverride, sizeof(RenderCamera)) == 0);
		if (same)
		{
			return;
		}
		m_SentCameraOverride = camera;
		submit([camera](RenderWorld& world)
			{
				world.get_scene().set_camera_override(camera);
			});
	}

	u64 RenderModule::request_pick(u32 x, u32 y)
	{
		const PickRequest request{ ++m_LastPickId, x, y };
		submit([request](RenderWorld& world)
			{
				world.get_renderer().set_pick_request(request);
			});
		return request.Id;
	}

	void RenderModule::set_selection(std::optional<EntityGuid> guid)
	{
		if (guid == m_SentSelection)
		{
			return;
		}
		m_SentSelection = guid;
		submit([guid](RenderWorld& world)
			{
				world.get_scene().set_selection(guid);
			});
	}

	void RenderModule::set_outline_style(const SelectionOutlineStyle& style)
	{
		const SelectionOutlineStyle& sent = m_SentOutlineStyle;
		if (style.Color == sent.Color && style.WidthPx == sent.WidthPx && style.OccludedAlpha == sent.OccludedAlpha && style.Enabled == sent.Enabled)
		{
			return;
		}
		m_SentOutlineStyle = style;
		submit([style](RenderWorld& world)
			{
				world.get_scene().set_outline_style(style);
			});
	}

	// Render thread: what the UI shows about the renderer.
	void RenderModule::publish_snapshot()
	{
		const auto now = std::chrono::steady_clock::now();
		if (m_StatsWindowStart == std::chrono::steady_clock::time_point{})
		{
			m_StatsWindowStart = now;
		}

		m_StatsFrames++;
		const double windowSeconds = std::chrono::duration<double>(now - m_StatsWindowStart).count();
		if (windowSeconds >= 0.5)
		{
			m_Fps = m_StatsFrames / windowSeconds;
			m_FrameMs = 1000.0 / m_Fps;
			m_StatsFrames = 0;
			m_StatsWindowStart = now;
		}

		RenderSnapshot snapshot;
		snapshot.Fps = m_Fps;
		snapshot.FrameMs = m_FrameMs;
		snapshot.Api = std::string(graphics_api_name(m_Renderer->get_api()));
		snapshot.Adapter = m_Renderer->get_adapter_name();
		snapshot.ShaderStatus = m_Renderer->get_shader_library()->get_status();
		snapshot.Vsync = m_Renderer->is_vsync_enabled();
		snapshot.ErrorCount = get_error_message_count();
		snapshot.HasCamera = m_Renderer->get_scene().effective_camera().has_value();
		{
			const SceneSettings& look = m_Renderer->get_scene().settings();
			snapshot.EnvironmentStatus = look.Source == EnvironmentSource::File ? m_Renderer->get_environments().status(look.EnvironmentAsset)
				: (look.Source == EnvironmentSource::Atmosphere ? "atmosphere: " + m_Renderer->get_environments().atmosphere_status() : std::string("none"));
		}
		snapshot.SceneInstances = static_cast<u32>(m_Renderer->get_scene().instance_count());
		snapshot.SceneLights = static_cast<u32>(m_Renderer->get_scene().light_count());
		const SceneRenderData& sceneData = m_Renderer->get_scene_data();
		snapshot.VisibleInstances = sceneData.visible_instances();
		snapshot.DrawCalls = static_cast<u32>(sceneData.opaque().size() + sceneData.masked().size() + sceneData.blended().size());
		snapshot.ShadowViews = static_cast<u32>(sceneData.shadow_views().size());
		const GpuResourceCache::Stats cacheStats = m_Renderer->get_cache().get_stats();
		snapshot.CacheMeshes = cacheStats.Meshes;
		snapshot.CacheMaterials = cacheStats.Materials;
		snapshot.CacheTextures = cacheStats.Textures;
		snapshot.CacheBytes = cacheStats.Bytes;
		snapshot.CachePending = cacheStats.Pending;
		snapshot.CacheFailed = cacheStats.Failed;
		const UploadQueue& uploads = m_Renderer->get_upload_queue();
		snapshot.UploadQueueName = uploads.is_using_copy_queue() ? "copy queue" : "graphics queue";
		snapshot.UploadBytesTotal = uploads.bytes_total();
		if (uploads.last_submit_ms() > 0.0)
		{
			m_UploadMs = uploads.last_submit_ms();
		}
		snapshot.UploadMs = m_UploadMs;
		for (const Scope<ViewState>& view : m_Renderer->extra_views())
		{
			if (view->Failed)
			{
				snapshot.FailedViews.push_back(view->Id);
			}
		}

		const bool gpu = m_Renderer->are_gpu_timings_available();
		for (const PassTiming& timing : m_Renderer->get_pass_timings())
		{
			const std::optional<float>& milliseconds = gpu ? timing.GpuMs : timing.CpuMs;
			snapshot.PassMs.emplace_back(timing.Name, milliseconds ? static_cast<double>(*milliseconds) : 0.0);
		}

		m_Snapshot.publish(std::move(snapshot));
	}

	void RenderModule::pre_update(float deltaTime)
	{
		if (m_Started && !m_Failed)
		{
			m_ImGui.begin_frame();
		}
	}

	void RenderModule::set_ui_preview(UiDrawList list, u32 width, u32 height, const glm::vec4& background)
	{
		if (!m_Started)
		{
			return;
		}
		auto shared = std::make_shared<UiDrawList>(std::move(list));
		UiRenderPass* pass = m_UiPass;
		submit([pass, shared, width, height, background](RenderWorld&)
			{
				if (pass != nullptr)
				{
					pass->set_preview(std::move(*shared), width, height, background);
				}
			});
	}

	void RenderModule::clear_ui_preview()
	{
		if (!m_Started)
		{
			return;
		}
		UiRenderPass* pass = m_UiPass;
		submit([pass](RenderWorld&)
			{
				if (pass != nullptr)
				{
					pass->clear_preview();
				}
			});
	}

	glm::vec2 RenderModule::get_canvas_size() const
	{
		if (m_RequestedViewportWidth > 0 && m_RequestedViewportHeight > 0)
		{
			return glm::vec2(static_cast<float>(m_RequestedViewportWidth), static_cast<float>(m_RequestedViewportHeight));
		}

		Window* window = Engine::get().get_module<Window>();
		if (window == nullptr)
		{
			return glm::vec2(0.0f);
		}
		int width = 0;
		int height = 0;
		window->get_size(width, height);
		return glm::vec2(static_cast<float>(std::max(width, 0)), static_cast<float>(std::max(height, 0)));
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

		{
			PerfStats& perf = Engine::get().get_perf();
			std::vector<std::pair<float, float>> samples;
			{
				std::lock_guard<std::mutex> lock(m_FrameSamplesMutex);
				samples.swap(m_FrameSamples);
			}
			for (const auto& [frameMs, cpuMs] : samples)
			{
				perf.add_render_frame(frameMs, cpuMs);
			}
			if (const std::string adapter = get_snapshot().Adapter; !adapter.empty())
			{
				perf.set_adapter_name(adapter);
			}
		}

		Window* window = Engine::get().get_module<Window>();
		int width = 0;
		int height = 0;
		window->get_size(width, height);

		RenderBatch batch;
		batch.FrameIndex = m_FrameIndex++;
		batch.DeltaTime = deltaTime;
		batch.DeltaSeconds = deltaTime * 0.001f; // update() gets milliseconds (Time::get_engine_delta_time)
		batch.Width = static_cast<u32>(std::max(width, 0));
		batch.Height = static_cast<u32>(std::max(height, 0));
		batch.Minimized = width == 0 || height == 0 || glfwGetWindowAttrib(window->get_window(), GLFW_ICONIFIED) != 0;
		batch.Commands = std::move(m_PendingCommands);
		m_PendingCommands.clear();
		batch.Debug = m_DebugDraw.seal(deltaTime);
		batch.Ui = m_GameUiVisible ? std::move(m_PendingUi) : UiDrawList{};
		m_PendingUi = UiDrawList{};

		// What the active scene changed, for the render thread's scene mirror.
		if (SceneModule* scenes = Engine::get().get_module<SceneModule>())
		{
			// The scene is drawn at the viewport's size when there is a Viewport panel, else at the window's.
			const u32 shapeWidth = m_RequestedViewportWidth > 0 ? m_RequestedViewportWidth : batch.Width;
			const u32 shapeHeight = m_RequestedViewportHeight > 0 ? m_RequestedViewportHeight : batch.Height;
			const float aspect = shapeHeight > 0 ? static_cast<float>(shapeWidth) / static_cast<float>(shapeHeight) : 1.0f;
			PaletteSource palettes;
			if (const AnimationModule* animation = Engine::get().get_module<AnimationModule>())
			{
				palettes = [animation](entt::entity animator) -> std::span<const glm::vec4>
				{
					const AnimatorInstance* instance = animation->instance_for(animator);
					if (instance == nullptr || instance->palette().empty())
					{
						return {};
					}
					return { reinterpret_cast<const glm::vec4*>(instance->palette().data()), instance->palette().size() * 3 };
				};
			}
			SceneDelta delta = m_Extractor.extract(scenes->get_active(), aspect, palettes);
			if (!delta.empty())
			{
				auto shared = std::make_shared<SceneDelta>(std::move(delta));
				batch.Commands.push_back([shared](RenderWorld& world)
					{
						world.get_scene().apply(std::move(*shared));
					});
			}
		}

		// The views of the ViewHost components of the scene in use are kept up to date first.
		if (SceneModule* scenes = Engine::get().get_module<SceneModule>())
		{
			m_ViewHosts.update(*this, scenes->get_active(), deltaTime);
		}

		// The offscreen views: what changed in their scenes, for their render-side scenes. A OneShot view is extracted only when it was asked
		// to render; the render thread draws it when it gets to it.
		if (!m_ViewRecords.empty())
		{
			m_FailedViews = get_snapshot().FailedViews;
		}
		for (auto& [requestId, image] : m_Readbacks.take())
		{
			const auto pending = m_PendingReadbacks.find(requestId);
			if (pending == m_PendingReadbacks.end())
			{
				continue; // its view is gone
			}
			std::function<void(ImageData)> done = std::move(pending->second.Done);
			m_PendingReadbacks.erase(pending);
			if (done)
			{
				done(std::move(image));
			}
		}
		for (const Scope<ViewRecord>& record : m_ViewRecords)
		{
			const bool wanted = record->Desc.Mode == ViewMode::Continuous || record->RenderPending;
			record->RenderPending = false;
			if (!wanted)
			{
				continue;
			}

			const Ref<Scene> scene = record->Scene.lock();
			if (scene == nullptr)
			{
				if (record->HadScene && !record->WarnedGone)
				{
					WI_RENDER_LOG(Warning, "The scene of view {0} is gone: the view keeps its last image", record->Id);
					record->WarnedGone = true;
				}
				continue;
			}

			scene->update_world_transforms();
			const float viewAspect = static_cast<float>(record->Width) / static_cast<float>(std::max(record->Height, 1u));
			// the palettes of a looked-at scene come from the animation module's preview instances of that scene
			PaletteSource viewPalettes;
			if (const AnimationModule* animation = Engine::get().get_module<AnimationModule>())
			{
				const Scene* viewScene = scene.get();
				viewPalettes = [animation, viewScene](entt::entity animator) -> std::span<const glm::vec4>
				{
					const AnimatorInstance* instance = animation->instance_in(viewScene, animator);
					if (instance == nullptr || instance->palette().empty())
					{
						return {};
					}
					return { reinterpret_cast<const glm::vec4*>(instance->palette().data()), instance->palette().size() * 3 };
				};
			}
			SceneDelta viewDelta = record->Extractor.extract(scene, viewAspect, viewPalettes);
			const ViewId viewId = record->Id;
			if (!viewDelta.empty())
			{
				auto shared = std::make_shared<SceneDelta>(std::move(viewDelta));
				batch.Commands.push_back([viewId, shared](RenderWorld& world)
					{
						if (ViewState* view = world.get_view(viewId))
						{
							view->Scene.apply(std::move(*shared));
						}
					});
			}
			if (record->CameraDirty)
			{
				record->CameraDirty = false;
				const RenderCamera camera = record->Camera;
				batch.Commands.push_back([viewId, camera](RenderWorld& world)
					{
						if (ViewState* view = world.get_view(viewId))
						{
							view->Scene.set_camera_override(camera);
						}
					});
			}
		}

		// The UI the modules built this frame, copied for the render thread.
		m_ImGui.end_frame(batch);

		// The font atlas is created on the render thread before the first frame that draws with it.
		u32 fontWidth = 0;
		u32 fontHeight = 0;
		std::vector<u8> fontPixels;
		if (m_ImGui.take_font_atlas(fontWidth, fontHeight, fontPixels))
		{
			auto pixels = std::make_shared<std::vector<u8>>(std::move(fontPixels));
			ImGuiRenderPass* pass = m_ImGuiPass;
			batch.Commands.insert(batch.Commands.begin(), [pass, fontWidth, fontHeight, pixels](RenderWorld&)
				{
					pass->set_font_atlas(fontWidth, fontHeight, pixels->data());
				});
		}

		if (m_SmokeFrames > 0 && smoke_ready())
		{
			if (++m_SmokeCounted >= m_SmokeFrames)
			{
				Engine::get().request_quit();
			}
		}

		if (m_RenderTestResult != 0)
		{
			Engine::get().set_exit_code(m_RenderTestResult == 1 ? 0 : 1);
			Engine::get().request_quit();
		}

		// Blocks while the render thread is two frames behind.
		if (!m_Thread.submit(std::move(batch)))
		{
			fail_and_quit("The render thread has stopped");
		}
	}

	bool RenderModule::read_arguments()
	{
		constexpr std::string_view apiPrefix = "--api=";

		// The project's launch settings of a game's pack come first; the command line below overrides them.
		const Engine& engine = Engine::get();
		if (engine.project_from_pak())
		{
			const ProjectSettings& project = engine.project();
			m_Description.Api = project.Api == LaunchApi::D3D11 ? GraphicsApi::D3D11 : project.Api == LaunchApi::D3D12 ? GraphicsApi::D3D12 : GraphicsApi::Vulkan;
			m_Description.EnableValidation = project.GpuValidation;
		}

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
			else if (argument == "--render-test")
			{
				m_RenderTest = true;
			}
			else if (argument == "--no-copy-queue")
			{
				m_Description.EnableCopyQueue = false;
			}
		}

		for (const std::string& argument : Engine::get().get_arguments())
		{
			m_SmokeHasScene = m_SmokeHasScene || argument == "--scene";
		}

		// --render-smoke <frames>
		const std::vector<std::string>& arguments = Engine::get().get_arguments();
		for (size_t i = 0; i < arguments.size(); i++)
		{
			if (arguments[i] == "--render-smoke")
			{
				m_SmokeFrames = (i + 1 < arguments.size()) ? std::strtoull(arguments[i + 1].c_str(), nullptr, 10) : 0;
				if (m_SmokeFrames == 0)
				{
					WI_RENDER_LOG(Error, "--render-smoke needs a number of frames");
					fail_and_quit("Invalid command line");
					return false;
				}
			}
		}

		return true;
	}

	// Render thread: --render-test runs its checks once, as soon as every pass can render (or after a wait that gives up).
	void RenderModule::run_render_test(RenderWorld& world)
	{
		if (!m_RenderTest || m_RenderTestResult != 0 || m_Failed)
		{
			return;
		}

		constexpr u32 MAX_WAIT_FRAMES = 1200;
		if (!m_Renderer->are_passes_ready() && ++m_RenderTestWaitFrames < MAX_WAIT_FRAMES)
		{
			return;
		}

		if (!g_RenderCheckRunner)
		{
			WI_RENDER_LOG(Error, "--render-test needs the checks of wiwa_tests: run it from wiwa_tests.exe");
			m_RenderTestResult = 2;
			return;
		}

		const bool passed = g_RenderCheckRunner(world) && m_Renderer->are_passes_ready() && get_error_message_count() == 0;
		if (passed)
		{
			WI_RENDER_LOG(Info, "Render test passed ({0})", graphics_api_name(m_Description.Api));
		}
		else
		{
			WI_RENDER_LOG(Error, "Render test FAILED ({0}, {1} GPU error message(s))", graphics_api_name(m_Description.Api), get_error_message_count());
		}
		m_RenderTestResult = passed ? 1 : 2;
	}

	void RenderModule::fail_and_quit(const char* reason)
	{
		WI_RENDER_LOG(Error, "{0}", reason);
		m_DebugDraw.set_enabled(false);
		Engine::get().set_exit_code(1);
		Engine::get().request_quit();
	}
}
