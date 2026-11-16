#include <pch.h>
#include "scene.h"

namespace wiwa
{
	Scene::Scene()
	{
		m_Registry.on_destroy<Id>().connect<&Scene::on_id_destroyed>(this);
	}

	void Scene::set_settings(SceneSettings settings)
	{
		settings = sanitized(std::move(settings));
		if (settings == m_Settings)
		{
			return;
		}
		m_Settings = std::move(settings);
		m_SettingsRevision++;
	}

	entt::entity Scene::create(EntityGuid id, std::string name, entt::entity parent)
	{
		const entt::entity entity = m_Registry.create();
		m_Registry.emplace<Id>(entity, Id{ id });
		m_Registry.emplace<Name>(entity, Name{ std::move(name) });
		m_Registry.emplace<Transform>(entity);
		m_Registry.emplace<Hierarchy>(entity);
		m_Registry.emplace<WorldTransform>(entity);
		m_ByGuid[id] = entity;

		if (parent != entt::null)
		{
			set_parent(entity, parent);
		}
		else
		{
			m_Roots.push_back(entity);
		}
		return entity;
	}

	entt::entity Scene::create(std::string name, entt::entity parent)
	{
		return create(EntityGuid::generate(), std::move(name), parent);
	}

	void Scene::destroy(entt::entity entity)
	{
		if (!m_Registry.valid(entity))
		{
			return;
		}

		unlink(entity);

		std::vector<entt::entity> subtree;
		for_each_descendant(entity, [&](entt::entity member) { subtree.push_back(member); });

		m_Registry.destroy(subtree.begin(), subtree.end());
	}

	void Scene::unlink(entt::entity child)
	{
		Hierarchy& link = m_Registry.get<Hierarchy>(child);
		if (link.Parent != entt::null)
		{
			Hierarchy& parent = m_Registry.get<Hierarchy>(link.Parent);
			if (parent.FirstChild == child)
			{
				parent.FirstChild = link.NextSibling;
			}
			else
			{
				for (entt::entity sibling = parent.FirstChild; sibling != entt::null; sibling = m_Registry.get<Hierarchy>(sibling).NextSibling)
				{
					Hierarchy& siblingLink = m_Registry.get<Hierarchy>(sibling);
					if (siblingLink.NextSibling == child)
					{
						siblingLink.NextSibling = link.NextSibling;
						break;
					}
				}
			}
		}

		else
		{
			std::erase(m_Roots, child);
		}

		link.Parent = entt::null;
		link.NextSibling = entt::null;
	}

	bool Scene::set_parent(entt::entity child, entt::entity parent)
	{
		if (!m_Registry.valid(child) || (parent != entt::null && !m_Registry.valid(parent)))
		{
			return false;
		}

		for (entt::entity ancestor = parent; ancestor != entt::null; ancestor = m_Registry.get<Hierarchy>(ancestor).Parent)
		{
			if (ancestor == child)
			{
				return false;
			}
		}

		unlink(child);
		if (parent == entt::null)
		{
			m_Roots.push_back(child);
			return true;
		}

		Hierarchy& parentLink = m_Registry.get<Hierarchy>(parent);
		if (parentLink.FirstChild == entt::null)
		{
			parentLink.FirstChild = child;
		}
		else
		{
			entt::entity last = parentLink.FirstChild;
			while (m_Registry.get<Hierarchy>(last).NextSibling != entt::null)
			{
				last = m_Registry.get<Hierarchy>(last).NextSibling;
			}
			m_Registry.get<Hierarchy>(last).NextSibling = child;
		}
		m_Registry.get<Hierarchy>(child).Parent = parent;
		return true;
	}

	bool Scene::move_after(entt::entity entity, entt::entity sibling)
	{
		if (!m_Registry.valid(entity) || !m_Registry.valid(sibling) || entity == sibling)
		{
			return false;
		}

		const entt::entity parent = m_Registry.get<Hierarchy>(sibling).Parent;
		for (entt::entity ancestor = sibling; ancestor != entt::null; ancestor = m_Registry.get<Hierarchy>(ancestor).Parent)
		{
			if (ancestor == entity)
			{
				return false;
			}
		}

		unlink(entity);
		Hierarchy& link = m_Registry.get<Hierarchy>(entity);
		link.Parent = parent;
		if (parent == entt::null)
		{
			m_Roots.insert(std::find(m_Roots.begin(), m_Roots.end(), sibling) + 1, entity);
		}
		else
		{
			Hierarchy& siblingLink = m_Registry.get<Hierarchy>(sibling);
			link.NextSibling = siblingLink.NextSibling;
			siblingLink.NextSibling = entity;
		}
		return true;
	}

	entt::entity Scene::find(EntityGuid id) const
	{
		const auto it = m_ByGuid.find(id);
		return it == m_ByGuid.end() ? entt::entity{ entt::null } : it->second;
	}

	void Scene::update_world_transforms()
	{
		for_each_in_order([&](entt::entity entity)
			{
				const Transform& local = m_Registry.get<Transform>(entity);
				glm::mat4 matrix = glm::translate(glm::mat4(1.0f), local.Position) * glm::mat4_cast(local.Rotation) * glm::scale(glm::mat4(1.0f), local.Scale);

				const Hierarchy& link = m_Registry.get<Hierarchy>(entity);
				if (link.Parent != entt::null)
				{
					matrix = m_Registry.get<WorldTransform>(link.Parent).Matrix * matrix;
				}
				m_Registry.get<WorldTransform>(entity).Matrix = matrix;
			});
	}

	size_t Scene::destroy_some(size_t maxEntities)
	{
		std::vector<entt::entity> doomed;
		for (const entt::entity entity : m_Registry.view<Id>())
		{
			if (doomed.size() >= maxEntities)
			{
				break;
			}
			doomed.push_back(entity);
		}

		m_Registry.destroy(doomed.begin(), doomed.end());
		m_Roots.clear(); // the scene is being thrown away: its hierarchy is not kept consistent
		return m_Registry.view<Id>().size();
	}

	void Scene::on_id_destroyed(entt::registry& registry, entt::entity entity)
	{
		m_ByGuid.erase(registry.get<Id>(entity).Guid);
	}
}
