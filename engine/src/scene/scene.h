#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "components.h"

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

		entt::entity find(EntityGuid id) const;

		// Recomputes every WorldTransform from the local ones.
		void update_world_transforms();

		// Destroys up to maxEntities entities in no particular order (the hierarchy is not kept consistent: the scene is being thrown
		// away). Returns how many entities are left. For taking a big scene down over several frames.
		size_t destroy_some(size_t maxEntities);

		// The top-level entities in the order they were made (or last moved to the top level), whatever was destroyed since.
		const std::vector<entt::entity>& roots() const { return m_Roots; }

		entt::registry& registry() { return m_Registry; }
		const entt::registry& registry() const { return m_Registry; }

	private:
		void unlink(entt::entity child);
		void on_id_destroyed(entt::registry& registry, entt::entity entity);

		entt::registry m_Registry{};
		std::unordered_map<EntityGuid, entt::entity> m_ByGuid{};
		std::vector<entt::entity> m_Roots{};
	};
}
