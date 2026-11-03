#include <pch.h>

#include "engine.h"
#include "memory.h"

#include <assets/asset_manager.h>
#include <physics/physics_module.h>
#include <renderer/render_module.h>
#include <renderer/window.h>
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

		m_Time.init();

		
		m_ModuleManager.add_module<AssetManager>();
		m_ModuleManager.add_module<SceneModule>();
		m_ModuleManager.add_module<PhysicsModule>(); // after the scene, before the scripts
		m_ModuleManager.add_module<Window>();
		m_ModuleManager.add_module<RenderModule>();
		m_ModuleManager.add_module<UiModule>(); // before the scripts: the clicks of a frame reach them in that frame
		m_ModuleManager.add_module<ScriptingModule>();

		on_init();
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
