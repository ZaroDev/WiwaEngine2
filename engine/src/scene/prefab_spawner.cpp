#include <pch.h>
#include "prefab_spawner.h"

#include "components.h"
#include "prefab_loading.h"
#include "scene_records.h"

#define WI_SCENE_LOG(level, ...) WI_LOG(level, "Scene", __VA_ARGS__)

namespace wiwa
{
	Handle<PrefabData> PrefabSpawner::prefab_handle(const AssetRef& ref)
	{
		const AssetGuid key = ref.resolved();
		auto found = m_Prefabs.find(key);
		if (found == m_Prefabs.end())
		{
			found = m_Prefabs.emplace(key, m_Assets.load<PrefabData>(ref)).first;
		}
		return found->second;
	}

	entt::entity PrefabSpawner::spawn(const Ref<Scene>& scene, const AssetRef& prefab, std::string name, entt::entity parent)
	{
		if (!prefab.valid() || scene == nullptr)
		{
			return entt::null;
		}

		std::lock_guard<std::recursive_mutex> lock(m_Mutex);
		const entt::entity root = scene->create(std::move(name), parent);
		scene->registry().emplace<PrefabInstance>(root, PrefabInstance{ prefab, {} });
		m_Pending.push_back(Pending{ scene->registry().get<Id>(root).Guid, scene, prefab_handle(prefab) });
		return root;
	}

	size_t PrefabSpawner::pending_count() const
	{
		std::lock_guard<std::recursive_mutex> lock(m_Mutex);
		return m_Pending.size();
	}

	void PrefabSpawner::clear()
	{
		std::lock_guard<std::recursive_mutex> lock(m_Mutex);
		m_Pending.clear();
	}

	bool PrefabSpawner::pending(EntityGuid root) const
	{
		std::lock_guard<std::recursive_mutex> lock(m_Mutex);
		for (const Pending& pending : m_Pending)
		{
			if (pending.Root == root)
			{
				return true;
			}
		}
		return false;
	}

	void PrefabSpawner::expand(Scene& scene, entt::entity root, const PrefabData& prefab)
	{
		const EntityRecord rootRecord = record_from_entity(scene, root);
		const PrefabResolver resolver = [this](const AssetRef& ref) -> const PrefabData* { return prefab_handle(ref).get(); };
		const ExpandResult expanded = expand_prefab(rootRecord, prefab, resolver);
		for (const std::string& warning : expanded.Warnings)
		{
			WI_SCENE_LOG(Warning, "{0}", warning);
		}

		for (size_t i = 1; i < expanded.Entities.size(); i++)
		{
			const entt::entity entity = apply_record(scene, expanded.Entities[i]);
			if (entity != entt::null)
			{
				scene.registry().emplace<PrefabChild>(entity, PrefabChild{ rootRecord.Id, expanded.NodeIds[i], expanded.NestedRoots[i] != 0 });
			}
		}
		scene.update_world_transforms();
	}

	void PrefabSpawner::update()
	{
		std::lock_guard<std::recursive_mutex> lock(m_Mutex);
		for (size_t i = 0; i < m_Pending.size();)
		{
			Pending& pending = m_Pending[i];
			const Ref<Scene> owner = pending.Owner.lock();
			if (owner == nullptr)
			{
				m_Pending.erase(m_Pending.begin() + static_cast<std::ptrdiff_t>(i)); // the scene is gone
				continue;
			}
			Scene& scene = *owner;
			const entt::entity root = scene.find(pending.Root);
			const LoadState state = pending.Prefab.state();
			if (root == entt::null)
			{
				m_Pending.erase(m_Pending.begin() + static_cast<std::ptrdiff_t>(i)); // the root was destroyed
			}
			else if (state == LoadState::Failed)
			{
				WI_SCENE_LOG(Error, "The prefab of an instance could not be loaded; '{0}' stays an empty entity", scene.registry().get<Name>(root).Value);
				m_Pending.erase(m_Pending.begin() + static_cast<std::ptrdiff_t>(i));
			}
			else if (state == LoadState::Ready)
			{
				const LoadState nested = nested_prefabs_state(*pending.Prefab.get(), [this](const AssetRef& ref) { return prefab_handle(ref); });
				if (nested == LoadState::Loading)
				{
					i++; // what the prefab instantiates is still loading
					continue;
				}
				expand(scene, root, *pending.Prefab.get());
				m_Pending.erase(m_Pending.begin() + static_cast<std::ptrdiff_t>(i));
			}
			else
			{
				i++;
			}
		}
	}
}
