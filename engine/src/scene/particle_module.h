#pragma once

#include <mutex>
#include <memory>
#include <unordered_map>
#include <vector>

#include <assets/asset_manager.h>
#include <core/module.h>
#include <assets/particle_effect.h>
#include "scene.h"

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
		virtual int get_priority() const override { return ModulePriority::Particle; }

		// The effect a reference names; null while it loads or when it cannot be loaded. Scripts and the editor ask it too.
		const ParticleEffectData* effect(const AssetRef& ref);

		// Shows `data` as the effect `ref` names, on the main thread and in the render thread, without a file (the VFX editor's unsaved edits): every system of that
		// effect in every scene and view plays it. clear_preview_effect gives the effect on disk back.
		void set_preview_effect(const AssetRef& ref, ParticleEffectData data);
		void clear_preview_effect(const AssetRef& ref);

		// Scenes that are only looked at (the VFX editor's preview scene): their systems play with real time, whatever the game clock does. Held weakly.
		void register_preview_scene(const Ref<Scene>& scene);
		void unregister_preview_scene(const Ref<Scene>& scene);

	private:
		AssetManager* m_Assets = nullptr;
		std::unordered_map<AssetGuid, Handle<ParticleEffectData>> m_Effects{};
		std::unordered_map<AssetGuid, Ref<const ParticleEffectData>> m_Overrides{};
		std::vector<std::weak_ptr<Scene>> m_PreviewScenes{};
		u32 m_ReloadListener = 0;
		std::mutex m_ReloadMutex{};
		std::vector<AssetGuid> m_ReloadQueue{};
	};
}
