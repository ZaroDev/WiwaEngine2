#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <assets/asset_manager.h>
#include <assets/handle.h>

#include "scene.h"

namespace wiwa
{
	// Puts prefab instances into a running scene (the Lua `instantiate_prefab`). The root entity of an instance is created at once with its
	// PrefabInstance, so the caller can place it; the entities the prefab generates (and what the prefabs it nests generate) are created by
	// update() as soon as everything they need has loaded, with the same expansion a scene load uses (prefab_expander.h) and a PrefabChild each.
	// Each queued instance remembers its scene (weakly), so it is expanded into the scene it was made in whichever scene is active when it is ready,
	// and dropped when that scene is gone. update() is called by SceneModule every frame before the scripts run, so a prefab that is already loaded is
	// complete at the start of the next frame. Scenes are main thread data: spawn and update run there; a lock keeps the queue itself safe for a
	// caller on another thread (a test).
	class PrefabSpawner
	{
	public:
		explicit PrefabSpawner(AssetManager& assets) : m_Assets(assets) {}

		// Creates the root (named `name`, under `parent` if it is not null) and queues its expansion. Returns entt::null when the prefab
		// reference is not valid. A prefab that fails to load leaves the root as an empty entity and logs one error.
		entt::entity spawn(const Ref<Scene>& scene, const AssetRef& prefab, std::string name, entt::entity parent = entt::null);

		// Expands the instances whose prefabs have loaded. An instance whose root is gone from its scene (destroyed), or whose scene is gone
		// (replaced and released), is dropped.
		void update();

		// Whether the instance with this root still waits for its entities (false for anything that is not a queued instance).
		bool pending(EntityGuid root) const;
		size_t pending_count() const;

		// Forgets every queued instance.
		void clear();

	private:
		struct Pending
		{
			EntityGuid Root{};
			std::weak_ptr<Scene> Owner{};
			Handle<PrefabData> Prefab{};
		};

		Handle<PrefabData> prefab_handle(const AssetRef& ref);
		void expand(Scene& scene, entt::entity root, const PrefabData& prefab);

		mutable std::recursive_mutex m_Mutex{};
		AssetManager& m_Assets;
		std::vector<Pending> m_Pending{};
		std::unordered_map<AssetGuid, Handle<PrefabData>> m_Prefabs{}; // keeps what the instances instantiate loaded
	};
}
