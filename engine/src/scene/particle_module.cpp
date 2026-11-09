#include <pch.h>
#include "particle_module.h"

#include <algorithm>

#include <core/engine.h>
#include <renderer/render_module.h>
#include <renderer/render_world.h>

#include "particle_runtime.h"
#include "scene_module.h"

namespace wiwa
{
	void ParticleModule::init()
	{
		m_Assets = Engine::get().get_module<AssetManager>();
		if (m_Assets == nullptr)
		{
			return;
		}

		// the listener runs on whatever thread cooked the asset: it only notes the guid
		m_ReloadListener = m_Assets->add_reload_listener([this](AssetGuid guid)
			{
				std::lock_guard<std::mutex> lock(m_ReloadMutex);
				m_ReloadQueue.push_back(guid);
			});
	}

	void ParticleModule::shutdown()
	{
		if (m_Assets != nullptr && m_ReloadListener != 0)
		{
			m_Assets->remove_reload_listener(m_ReloadListener);
			m_ReloadListener = 0;
		}
		m_Effects.clear();
	}

	const ParticleEffectData* ParticleModule::effect(const AssetRef& ref)
	{
		if (m_Assets == nullptr || !ref.valid())
		{
			return nullptr;
		}
		if (const auto override = m_Overrides.find(ref.resolved()); override != m_Overrides.end())
		{
			return override->second.get();
		}
		auto it = m_Effects.find(ref.resolved());
		if (it == m_Effects.end())
		{
			it = m_Effects.emplace(ref.resolved(), m_Assets->load<ParticleEffectData>(ref)).first;
		}
		return it->second.get();
	}

	void ParticleModule::update(float)
	{
		// an effect that was cooked again: the next lookup loads the new one
		std::vector<AssetGuid> reloaded;
		{
			std::lock_guard<std::mutex> lock(m_ReloadMutex);
			reloaded.swap(m_ReloadQueue);
		}
		for (const AssetGuid guid : reloaded)
		{
			m_Effects.erase(guid);
		}

		const Time& time = Engine::get().get_time();
		const ParticleEffectLookup lookup = [this](const AssetRef& ref) { return effect(ref); };

		// scenes that are only looked at play with real time
		for (auto it = m_PreviewScenes.begin(); it != m_PreviewScenes.end();)
		{
			if (const Ref<Scene> preview = it->lock())
			{
				tick_particle_systems(*preview, time.get_real_delta_time_seconds(), lookup);
				++it;
			}
			else
			{
				it = m_PreviewScenes.erase(it);
			}
		}

		SceneModule* scenes = Engine::get().get_module<SceneModule>();
		if (scenes == nullptr)
		{
			return;
		}
		const Ref<Scene> scene = scenes->get_active();
		if (scene == nullptr)
		{
			return;
		}
		if (time.get_game_clock().IsPlaying)
		{
			tick_particle_systems(*scene, time.get_delta_time_seconds(), lookup);
		}
		else
		{
			// edit mode: only what the user asked to preview (the inspector's Play, the VFX editor)
			tick_particle_systems(*scene, time.get_real_delta_time_seconds(), lookup, true);
		}
	}

	void ParticleModule::set_preview_effect(const AssetRef& ref, ParticleEffectData data)
	{
		if (!ref.valid())
		{
			return;
		}
		m_Overrides[ref.resolved()] = std::make_shared<const ParticleEffectData>(data);
		if (RenderModule* render = Engine::get().get_module<RenderModule>())
		{
			render->submit([ref, data = std::move(data)](RenderWorld& world)
				{
					world.get_cache().set_inline_particle_effect(ref, data);
				});
		}
	}

	void ParticleModule::clear_preview_effect(const AssetRef& ref)
	{
		if (!ref.valid() || m_Overrides.erase(ref.resolved()) == 0)
		{
			return;
		}
		if (RenderModule* render = Engine::get().get_module<RenderModule>())
		{
			render->submit([ref](RenderWorld& world)
				{
					world.get_cache().clear_inline_particle_effect(ref);
				});
		}
	}

	void ParticleModule::register_preview_scene(const Ref<Scene>& scene)
	{
		if (scene != nullptr && std::none_of(m_PreviewScenes.begin(), m_PreviewScenes.end(), [&](const std::weak_ptr<Scene>& held) { return held.lock() == scene; }))
		{
			m_PreviewScenes.push_back(scene);
		}
	}

	void ParticleModule::unregister_preview_scene(const Ref<Scene>& scene)
	{
		m_PreviewScenes.erase(std::remove_if(m_PreviewScenes.begin(), m_PreviewScenes.end(), [&](const std::weak_ptr<Scene>& held) { const Ref<Scene> alive = held.lock(); return alive == nullptr || alive == scene; }), m_PreviewScenes.end());
	}
}
