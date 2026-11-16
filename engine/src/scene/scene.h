#pragma once

#include <algorithm>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "components.h"
#include "scene_settings.h"

namespace wiwa
{
	// The entities of a level: an EnTT registry plus the hierarchy and the lookup by EntityGuid. Main thread only.
	class Scene
	{
	public:
		Scene();
		~Scene() = default;

		Scene(const Scene&) = delete;
		Scene& operator=(const Scene&) = delete;

		// Every entity has Id, Name, Transform, Hierarchy and WorldTransform. The second form makes up a new guid.
		entt::entity create(EntityGuid id, std::string name, entt::entity parent = entt::null);
		entt::entity create(std::string name, entt::entity parent = entt::null);

		// The entity and everything below it.
		void destroy(entt::entity entity);

		// Moves child under parent (entt::null for the top level), as the last child. False, and nothing changes, if that would
		// make an entity its own ancestor.
		bool set_parent(entt::entity child, entt::entity parent);

		// Moves entity to the parent of sibling, right after it. False, and nothing changes, if that would make an entity its own ancestor.
		bool move_after(entt::entity entity, entt::entity sibling);

		entt::entity find(EntityGuid id) const;

		// Recomputes every WorldTransform from the local ones.
		void update_world_transforms();

		// Destroys up to maxEntities entities in no particular order (the hierarchy is not kept consistent: the scene is being thrown
		// away). Returns how many entities are left. For taking a big scene down over several frames.
		size_t destroy_some(size_t maxEntities);

		// Calls visit(entity) for the entity and everything below it: parents before children, siblings in order. A visit that
		// returns bool (rather than nothing) says whether to go on below that entity: false leaves its children out. The hierarchy
		// must not change during the walk.
		template<typename F>
		void for_each_descendant(entt::entity entity, F&& visit) const
		{
			std::vector<entt::entity> stack{ entity };
			while (!stack.empty())
			{
				const entt::entity current = stack.back();
				stack.pop_back();
				if constexpr (std::is_void_v<std::invoke_result_t<F&, entt::entity>>)
				{
					visit(current);
				}
				else if (!visit(current))
				{
					continue;
				}

				// children are pushed in reverse so that they come out in sibling order
				const size_t first = stack.size();
				for (entt::entity child = m_Registry.get<Hierarchy>(current).FirstChild; child != entt::null; child = m_Registry.get<Hierarchy>(child).NextSibling)
				{
					stack.push_back(child);
				}
				std::reverse(stack.begin() + static_cast<std::ptrdiff_t>(first), stack.end());
			}
		}

		// for_each_descendant of every top-level entity, in order: the whole scene, parents before children.
		template<typename F>
		void for_each_in_order(F&& visit) const
		{
			for (const entt::entity root : m_Roots)
			{
				for_each_descendant(root, visit);
			}
		}

		// The top-level entities in the order they were made (or last moved to the top level), whatever was destroyed since.
		const std::vector<entt::entity>& roots() const { return m_Roots; }

		// How the scene is lit and shown. set_settings() sanitizes the value and bumps settings_revision() when it differs from the
		// current one, so that whoever mirrors the settings (the SceneExtractor) can tell cheaply.
		const SceneSettings& settings() const { return m_Settings; }
		void set_settings(SceneSettings settings);
		u64 settings_revision() const { return m_SettingsRevision; }

		entt::registry& registry() { return m_Registry; }
		const entt::registry& registry() const { return m_Registry; }

	private:
		void unlink(entt::entity child);
		void on_id_destroyed(entt::registry& registry, entt::entity entity);

		entt::registry m_Registry{};
		std::unordered_map<EntityGuid, entt::entity> m_ByGuid{};
		std::vector<entt::entity> m_Roots{};
		SceneSettings m_Settings{};
		u64 m_SettingsRevision = 1;
	};
}
