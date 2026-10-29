#include <pch.h>
#include "scene.h"

namespace wiwa
{
	Scene::Scene()
	{
		m_Registry.on_destroy<Id>().connect<&Scene::on_id_destroyed>(this);
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

		std::vector<entt::entity> subtree{ entity };
		for (size_t i = 0; i < subtree.size(); i++)
		{
			for (entt::entity child = m_Registry.get<Hierarchy>(subtree[i]).FirstChild; child != entt::null; child = m_Registry.get<Hierarchy>(child).NextSibling)
			{
				subtree.push_back(child);
			}
		}

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

	entt::entity Scene::find(EntityGuid id) const
	{
		const auto it = m_ByGuid.find(id);
		return it == m_ByGuid.end() ? entt::entity{ entt::null } : it->second;
	}

	void Scene::update_world_transforms()
	{
		std::vector<entt::entity> stack;
		for (const entt::entity root : m_Registry.view<Hierarchy>())
		{
			if (m_Registry.get<Hierarchy>(root).Parent != entt::null)
			{
				continue;
			}

			stack.push_back(root);
			while (!stack.empty())
			{
				const entt::entity entity = stack.back();
				stack.pop_back();

				const Transform& local = m_Registry.get<Transform>(entity);
				glm::mat4 matrix = glm::translate(glm::mat4(1.0f), local.Position) * glm::mat4_cast(local.Rotation) * glm::scale(glm::mat4(1.0f), local.Scale);

				const Hierarchy& link = m_Registry.get<Hierarchy>(entity);
				if (link.Parent != entt::null)
				{
					matrix = m_Registry.get<WorldTransform>(link.Parent).Matrix * matrix;
				}
				m_Registry.get<WorldTransform>(entity).Matrix = matrix;

				for (entt::entity child = link.FirstChild; child != entt::null; child = m_Registry.get<Hierarchy>(child).NextSibling)
				{
					stack.push_back(child);
				}
			}
		}
	}

	void Scene::on_id_destroyed(entt::registry& registry, entt::entity entity)
	{
		m_ByGuid.erase(registry.get<Id>(entity).Guid);
	}
}
