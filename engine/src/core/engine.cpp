#include <pch.h>

#include "engine.h"
#include "memory.h"

#include <assets/asset_manager.h>
#include <animation/animation_module.h>
#include <audio/audio_module.h>
#include <input/input_module.h>
#include <nav/nav_module.h>
#include <physics/physics_module.h>
#include <renderer/render_module.h>
#include <renderer/window.h>
#include <scene/decal_module.h>
#include <scene/particle_module.h>
#include <scene/scene_module.h>
#include <scripting/script_module.h>
#include <ui/ui_module.h>



#define WI_ENGINE_LOG(level, ...) WI_LOG(level, "Engine", __VA_ARGS__)

namespace wiwa 
{
	Engine::Engine(int argc, char** argv) 
	{
		for (int i = 1; i < argc; i++)
		{
			m_Arguments.emplace_back(argv[i]);
		}
	}

	Engine::~Engine()
	{
		
	}

	void Engine::init()
	{
		Log::init();
		WI_ENGINE_LOG(Info, "Initializing engine");
		WI_ENGINE_LOG(Info, "Allocated {0} bytes with {1} allocations", s_AllocatedBytesSize.load(), s_AllocationCount.load());

		s_Engine = this;
		m_ModuleManager.set_perf_stats(&m_Perf);

		m_Time.init();

		
		AssetManager* assets = m_ModuleManager.add_module<AssetManager>();
		load_project(*assets);
		if (m_ProjectFromPak && m_Project.FpsCap > 0)
		{
			m_Time.set_target_fps(static_cast<int>(m_Project.FpsCap)); // the editor is not capped by the project
		}
		m_ModuleManager.add_module<SceneModule>();
		m_ModuleManager.add_module<PhysicsModule>(); // after the scene, before the scripts
		m_ModuleManager.add_module<Window>();
		m_ModuleManager.add_module<InputModule>(); // after the window (it listens to it), before the render module whose ImGui backend chains onto its callbacks
		m_ModuleManager.add_module<RenderModule>();
		m_ModuleManager.add_module<UiModule>(); // before the scripts: the clicks of a frame reach them in that frame
		m_ModuleManager.add_module<AnimationModule>(); // runs after the scripts (priority 10) but must exist when they start: they ask for it
		m_ModuleManager.add_module<AudioModule>(); // must exist when the scripts start: they ask for it. It runs after them and the animation (priority 20)
		m_ModuleManager.add_module<NavModule>(); // must exist when the scripts start: they ask for it. It runs after them (priority 5) and before the animation
		m_ModuleManager.add_module<ScriptingModule>();
		m_ModuleManager.add_module<ParticleModule>(); // after the scripts (priority 12): an effect they spawn gets its first spawn counts in the frame it appears
		m_ModuleManager.add_module<DecalModule>(); // after the scripts (priority 11): a decal they spawn is aged in the frame it appears

		on_init();
	}

	void Engine::load_project(AssetManager& assets)
	{
		if (IAssetSource* source = assets.get_source())
		{
			if (const std::optional<ProjectSettings> packed = read_project_settings(*source))
			{
				m_Project = *packed;
				m_ProjectFromPak = true;
				return;
			}
		}

		std::string error;
		if (const std::optional<ProjectSettings> loaded = load_project_settings(PROJECT_SETTINGS_FILE, error))
		{
			m_Project = *loaded;
		}
		else if (!error.empty())
		{
			WI_ENGINE_LOG(Warning, "{0}: the default project settings are used", error);
		}
	}

	void Engine::update()
	{
		m_Time.update();
		while (m_Running)
		{
			m_Time.pre_update();
			if (m_Time.get_target_delta_time() > 0.0f)
			{
				auto time = m_Time.get_real_clock().get_last_time();
				auto lastTime = m_Time.get_engine_clock().get_last_time();
				std::chrono::duration<float> currentDelta = time - lastTime;
				float evaltime = currentDelta.count() * 1000.0f;
				if (evaltime < m_Time.get_target_delta_time())
				{
					continue;
				}
			}
			const auto frameStart = std::chrono::steady_clock::now();
			m_Perf.begin_frame();
			m_Time.update();

			m_ModuleManager.begin();

			float realDeltaTime = m_Time.get_engine_delta_time();

			// Update
			m_ModuleManager.pre_update(realDeltaTime);
			m_ModuleManager.update(realDeltaTime);
			m_ModuleManager.post_update(realDeltaTime);

			// Physics update
			m_ModuleManager.pre_physics_update(realDeltaTime);
			m_ModuleManager.physics_update(realDeltaTime);
			m_ModuleManager.post_physics_update(realDeltaTime);

			// Renderer update
			m_ModuleManager.pre_render_update(realDeltaTime);
			m_ModuleManager.render_update(realDeltaTime);
			m_ModuleManager.post_render_update(realDeltaTime);

			on_update(realDeltaTime);

			m_Perf.end_frame(std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - frameStart).count());
		}
	}

	void Engine::shutdown()
	{
		m_ModuleManager.clean();
		on_shutdown();

		WI_ENGINE_LOG(Info, "Shutdown");
		WI_ENGINE_LOG(Info, "Allocated {0} bytes with {1} allocations", s_AllocatedBytesSize.load(), s_AllocationCount.load());
	}

	void Engine::request_quit()
	{
		m_Running = false;
	}
}
