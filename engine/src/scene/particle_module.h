#pragma once

#include <mutex>
#include <unordered_map>
#include <vector>

#include <assets/asset_manager.h>
#include <core/module.h>
#include <assets/particle_effect.h>

namespace wiwa
{
	// Runs the ParticleSystems of the active scene while the game clock plays (particle_runtime.h). After the scripts (priority 12), so an effect a script
	// spawned this frame already has its first spawn counts when the frame is extracted for rendering. Loads the effects it needs and keeps them.
	class ParticleModule : public IModule
	{
	public:
		virtual void init() override;
		virtual void shutdown() override;
		virtual void update(float deltaTime) override;

		virtual UpdateFlags get_update_flags() const override { return UpdateFlags::UPDATE; }
		virtual std::string get_name() const override { return "ParticleModule"; }
		virtual int get_priority() const override { return 12; }

		// The effect a reference names; null while it loads or when it cannot be loaded. Scripts and the editor ask it too.
		const ParticleEffectData* effect(const AssetRef& ref);

	private:
		AssetManager* m_Assets = nullptr;
		std::unordered_map<AssetGuid, Handle<ParticleEffectData>> m_Effects{};
		u32 m_ReloadListener = 0;
		std::mutex m_ReloadMutex{};
		std::vector<AssetGuid> m_ReloadQueue{};
	};
}
