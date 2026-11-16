#include <pch.h>
#include "decal_module.h"

#include <core/engine.h>

#include "decal_lifetime.h"
#include "scene_module.h"

namespace wiwa
{
	void DecalModule::update(float)
	{
		const Time& time = Engine::get().get_time();
		if (!time.get_game_clock().IsPlaying)
		{
			return;
		}

		SceneModule* scenes = Engine::get().get_module<SceneModule>();
		if (scenes == nullptr)
		{
			return;
		}
		if (const Ref<Scene> scene = scenes->get_active())
		{
			tick_decals(*scene, time.get_delta_time_seconds());
		}
	}
}
