#include <pch.h>
#include "render_module.h"

#include <GLFW/glfw3.h>

#include <assets/asset_manager.h>
#include <core/engine.h>
#include <scene/scene_module.h>

#include "passes/blit_pass.h"
#include "passes/depth_prepass.h"
#include "passes/forward_pass.h"
#include "passes/imgui_pass.h"
#include "passes/light_cull_pass.h"
#include "passes/resolve_pass.h"
#include "passes/shadow_pass.h"
#include "shader_library.h"
#include "render_world.h"
#include "test/render_test.h"
#include "window.h"

#define WI_RENDER_LOG(level, ...) WI_LOG(level, "Renderer", __VA_ARGS__)

namespace
{
	constexpr const char* QUALITY_FILE = "render_quality.json";
}

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

				// The scene first (depth, shading, the 8-bit image); the UI goes over everything else, so its pass is the last one.
				Scope<ImGuiRenderPass> imguiPass = CreateScope<ImGuiRenderPass>();
				m_ImGuiPass = imguiPass.get();
				return m_Renderer->add_pass(CreateScope<ShadowPass>())
					&& m_Renderer->add_pass(CreateScope<DepthPrepass>())
					&& m_Renderer->add_pass(CreateScope<LightCullPass>())
					&& m_Renderer->add_pass(CreateScope<ForwardPass>())
					&& m_Renderer->add_pass(CreateScope<ResolvePass>())
					&& m_Renderer->add_pass(CreateScope<BlitPass>())
					&& m_Renderer->add_pass(std::move(imguiPass));
			},
			[this](RenderBatch& batch)
			{
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

		m_Thread.stop();
		m_Renderer.reset();
		m_ImGuiPass = nullptr;
		m_ImGui.shutdown();
		m_Started = false;

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

		// What the active scene changed, for the render thread's scene mirror.
		if (SceneModule* scenes = Engine::get().get_module<SceneModule>())
		{
			// The scene is drawn at the viewport's size when there is a Viewport panel, else at the window's.
			const u32 shapeWidth = m_RequestedViewportWidth > 0 ? m_RequestedViewportWidth : batch.Width;
			const u32 shapeHeight = m_RequestedViewportHeight > 0 ? m_RequestedViewportHeight : batch.Height;
			const float aspect = shapeHeight > 0 ? static_cast<float>(shapeWidth) / static_cast<float>(shapeHeight) : 1.0f;
			SceneDelta delta = m_Extractor.extract(scenes->get_active(), aspect);
			if (!delta.empty())
			{
				auto shared = std::make_shared<SceneDelta>(std::move(delta));
				batch.Commands.push_back([shared](RenderWorld& world)
					{
						world.get_scene().apply(std::move(*shared));
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

		const bool passed = run_render_checks(world) && m_Renderer->are_passes_ready() && get_error_message_count() == 0;
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
		Engine::get().set_exit_code(1);
		Engine::get().request_quit();
	}
}
