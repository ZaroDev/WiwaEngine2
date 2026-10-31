#include <pch.h>
#include "script_module.h"

#include <core/engine.h>
#include <renderer/render_module.h>
#include <scene/scene_module.h>

#include "script_component.h"

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
		if (RenderModule* renderer = Engine::get().get_module<RenderModule>())
		{
			context.Debug = &renderer->get_debug_draw();
		}

		register_catalog_scripts();
	}

	void ScriptingModule::shutdown()
	{
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

		// seconds of the game clock, which follows pause and time scale
		m_Instances->update(Engine::get().get_time().get_delta_time_seconds());
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
