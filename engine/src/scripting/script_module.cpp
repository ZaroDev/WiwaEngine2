#include <pch.h>
#include "script_module.h"

#include <ui/ui_module.h>

#include <algorithm>

#include <core/engine.h>
#include <renderer/render_module.h>
#include <physics/physics_module.h>
#include <scene/scene_module.h>

#include "script_component.h"
#include "script_reload.h"

#define WI_SCRIPT_LOG(level, ...) WI_LOG(level, "Scripting", __VA_ARGS__)

namespace wiwa
{
	void ScriptingModule::init()
	{
		AssetManager* assets = Engine::get().get_module<AssetManager>();
		m_Source = CreateScope<AssetScriptSource>(*assets);
		m_Runtime = CreateScope<ScriptRuntime>(*m_Source);
		m_Instances = CreateScope<ScriptInstances>(*m_Runtime);

		ScriptContext& context = m_Instances->context();
		context.Assets = assets;
		context.Scenes = Engine::get().get_module<SceneModule>();
		context.Ui = Engine::get().get_module<UiModule>();
		context.Physics = Engine::get().get_module<PhysicsModule>();
		if (RenderModule* renderer = Engine::get().get_module<RenderModule>())
		{
			context.Debug = &renderer->get_debug_draw();
		}

		register_catalog_scripts();

		// the listener runs on whatever thread cooked the asset: it only notes the guid
		m_ReloadListener = assets->add_reload_listener([this](AssetGuid guid)
			{
				std::lock_guard<std::mutex> lock(m_ReloadMutex);
				m_ReloadQueue.push_back(guid);
			});
	}

	void ScriptingModule::shutdown()
	{
		if (AssetManager* assets = Engine::get().get_module<AssetManager>())
		{
			assets->remove_reload_listener(m_ReloadListener);
		}
		stop();
		m_Instances.reset();
		m_Runtime.reset();
		m_Source.reset();
	}

	// A scene file names its scripts by asset, and the scene is applied before any script has been used: every script of the catalog is
	// loaded now, so that their components exist when entities are made. (The editor registers them when it imports them as well.)
	void ScriptingModule::register_catalog_scripts()
	{
		AssetManager* assets = Engine::get().get_module<AssetManager>();
		u32 registered = 0;
		for (const AssetCatalog::Entry& entry : assets->catalog_entries())
		{
			if (!entry.Path.ends_with(".lua"))
			{
				continue;
			}

			std::string error;
			if (ScriptClass* scriptClass = m_Runtime->load(AssetRef{ entry.Guid, 0 }, error))
			{
				register_script_component(*scriptClass);
				registered++;
			}
			else
			{
				WI_SCRIPT_LOG(Error, "{0}: {1}", entry.Path, error);
			}
		}
		if (registered > 0)
		{
			WI_SCRIPT_LOG(Info, "Loaded {0} script(s)", registered);
		}
	}

	// A script that was cooked again (the editor imported a change): its new code replaces the old in place, and the scripts that
	// require it follow. Asset guids that are not scripts this runtime has run (shaders, textures, a script nobody used) are ignored.
	void ScriptingModule::process_reloads()
	{
		std::vector<AssetGuid> guids;
		{
			std::lock_guard<std::mutex> lock(m_ReloadMutex);
			guids.swap(m_ReloadQueue);
		}

		SceneModule* scenes = Engine::get().get_module<SceneModule>();
		for (size_t i = 0; i < guids.size() && scenes != nullptr; i++)
		{
			const AssetGuid guid = guids[i];
			if (std::find(guids.begin(), guids.begin() + static_cast<std::ptrdiff_t>(i), guid) != guids.begin() + static_cast<std::ptrdiff_t>(i))
			{
				continue; // said twice
			}

			// a script the runtime ran, one it failed to use, or any script of the catalog: one that failed the first time is fixed by this save
			const AssetRef ref{ guid, 0 };
			if (!m_Runtime->knows(ref) && !m_Instances->is_broken(guid))
			{
				AssetManager* assets = Engine::get().get_module<AssetManager>();
				if (assets == nullptr || !is_script_asset(assets->catalog_entries(), guid))
				{
					continue;
				}
			}

			m_Source->forget(ref);
			std::string error;
			std::vector<AssetRef> reloaded;
			entt::registry* registry = scenes != nullptr ? &scenes->get_active()->registry() : nullptr;
			if (m_Runtime->reload_script(ref, error, registry, &reloaded))
			{
				m_Instances->script_reloaded(reloaded);
				WI_SCRIPT_LOG(Info, "Reloaded {0} script(s)", reloaded.size());
			}
			else
			{
				WI_SCRIPT_LOG(Error, "Reload failed, the previous version stays: {0}", error);
			}
		}
	}

	void ScriptingModule::start()
	{
		SceneModule* scenes = Engine::get().get_module<SceneModule>();
		if (m_Instances != nullptr && scenes != nullptr && !m_Instances->running())
		{
			m_Instances->start(scenes->get_active());
		}
	}

	void ScriptingModule::stop()
	{
		if (m_Instances != nullptr)
		{
			m_Instances->stop();
		}
	}

	// The scripts follow the active scene: when a load replaced it, the old scene's scripts end (while that scene is still the one they
	// know) and the new scene's begin.
	void ScriptingModule::follow_active_scene()
	{
		SceneModule* scenes = Engine::get().get_module<SceneModule>();
		if (scenes == nullptr)
		{
			return;
		}

		const Ref<Scene> active = scenes->get_active();
		if (m_Instances->running() && m_Instances->scene() != active)
		{
			m_Instances->stop();
		}
		if (!m_Instances->running())
		{
			m_Instances->start(active);
		}
	}

	void ScriptingModule::update(float)
	{
		if (m_Instances == nullptr)
		{
			return;
		}

		process_reloads();

		if (m_AutoStart)
		{
			follow_active_scene();
		}
		else if (m_Instances->running())
		{
			// play mode: a scene that was replaced ends the run, and play mode decides what happens next
			SceneModule* scenes = Engine::get().get_module<SceneModule>();
			if (scenes != nullptr && m_Instances->scene() != scenes->get_active())
			{
				m_Instances->stop();
			}
		}

		// seconds of the game clock, which follows pause and time scale; while it is paused the scripts do not run
		const Time& time = Engine::get().get_time();
		if (time.get_game_clock().IsPlaying)
		{
			if (PhysicsModule* physics = Engine::get().get_module<PhysicsModule>())
			{
				m_Instances->dispatch_physics(*physics);
			}
			m_Instances->update(time.get_delta_time_seconds());
		}
	}

	// Scripts write local transforms; the renderer reads the world ones.
	void ScriptingModule::post_update(float)
	{
		if (m_Instances != nullptr && m_Instances->running())
		{
			m_Instances->scene()->update_world_transforms();
		}
	}
}
