#include <pch.h>
#include "scene_loader.h"

#include <chrono>

#include "prefab_expander.h"

namespace wiwa
{
	std::optional<AssetRef> prefab_of(const EntityRecord& entity)
	{
		for (const ComponentRecord& component : entity.Components)
		{
			if (component.Name != PREFAB_INSTANCE_COMPONENT)
			{
				continue;
			}

			for (const FieldRecord& field : component.Fields)
			{
				if (field.Name == "Prefab")
				{
					if (const AssetRef* ref = std::get_if<AssetRef>(&field.Value); ref != nullptr && ref->valid())
					{
						return *ref;
					}
				}
			}
		}
		return std::nullopt;
	}

	SceneBlueprint build_blueprint(const SceneData& data, const PrefabLookup& prefabs)
	{
		SceneBlueprint blueprint;
		blueprint.Name = data.Name;

		for (const EntityRecord& entity : data.Entities)
		{
			const std::optional<AssetRef> prefabRef = prefab_of(entity);
			const PrefabData* prefab = prefabRef ? prefabs(*prefabRef) : nullptr;
			if (prefab == nullptr)
			{
				if (prefabRef)
				{
					blueprint.Warnings.push_back("entity " + entity.Id.to_string() + ": its prefab is not available, the instance has no children");
				}
				blueprint.Entities.push_back(entity);
				blueprint.Origins.push_back(EntityOrigin{});
				continue;
			}

			ExpandResult expanded = expand_prefab(entity, *prefab, prefabs);
			blueprint.Warnings.insert(blueprint.Warnings.end(), expanded.Warnings.begin(), expanded.Warnings.end());
			for (size_t i = 0; i < expanded.Entities.size(); i++)
			{
				blueprint.Entities.push_back(std::move(expanded.Entities[i]));
				blueprint.Origins.push_back(i == 0 ? EntityOrigin{} : EntityOrigin{ entity.Id, expanded.NodeIds[i], expanded.NestedRoots[i] != 0 });
			}
		}

		blueprint.References = collect_references(blueprint.Entities);
		blueprint.Settings = data.Settings;
		if (data.Settings.Source == EnvironmentSource::File && data.Settings.EnvironmentAsset.valid())
		{
			blueprint.References.push_back(data.Settings.EnvironmentAsset); // loaded with the rest before the scene shows
		}
		return blueprint;
	}

	SceneCommit::SceneCommit(Scene& target, SceneBlueprint blueprint)
		: m_Target(target), m_Blueprint(std::move(blueprint))
	{
		m_Created.reserve(m_Blueprint.Entities.size());
	}

	bool SceneCommit::step(float budgetMs)
	{
		const auto start = std::chrono::steady_clock::now();
		while (m_Next < m_Blueprint.Entities.size())
		{
			const entt::entity entity = apply_record(m_Target, m_Blueprint.Entities[m_Next], &m_Report);
			if (entity != entt::null)
			{
				const EntityOrigin& origin = m_Blueprint.Origins[m_Next];
				if (origin.Instance.valid())
				{
					m_Target.registry().emplace<PrefabChild>(entity, PrefabChild{ origin.Instance, origin.NodeId, origin.NestedRoot });
				}
				m_Created.push_back(entity);
			}
			m_Next++;

			const float elapsed = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
			if (elapsed >= budgetMs)
			{
				break;
			}
		}
		return m_Next >= m_Blueprint.Entities.size();
	}
}
