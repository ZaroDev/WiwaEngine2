#include <pch.h>
#include "particle_module.h"

#include <core/engine.h>

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
			tick_particle_systems(*scene, time.get_delta_time_seconds(), [this](const AssetRef& ref) { return effect(ref); });
		}
	}
}
