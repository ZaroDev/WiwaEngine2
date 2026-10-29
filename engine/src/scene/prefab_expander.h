#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <assets/asset_data.h>
#include "scene_data.h"

// Linked prefab instances, on records: the entities of an instance are generated from a Prefab, and the instance's
// overrides change them. Nothing here touches a Scene or the ComponentRegistry, so it can run on a worker thread.
namespace wiwa
{
	// NodeIds value of an entity that an AddChild override made.
	constexpr u64 ADDED_CHILD_NODE = ~0ull;

	struct ExpandResult
	{
		std::vector<EntityRecord> Entities{}; // the instance root first, then every entity of the instance, parents before children
		std::vector<u64> NodeIds{};           // parallel to Entities: 0 for the root, the prefab node's StableId, or ADDED_CHILD_NODE
		std::vector<std::string> Warnings{};
		std::vector<u64> DroppedOverrideNodes{}; // nodes the overrides name that the prefab does not have (any more)
	};

	// instanceRoot is the record of the entity that holds the PrefabInstance. Its own record comes back first, with its overrides
	// kept (so the scene can be saved again); the node entities get the id derived from the root's id and the node's StableId.
	// Overrides are applied in order. An override on a node the prefab does not have is dropped with a warning, never applied
	// to another node.
	ExpandResult expand_prefab(const EntityRecord& instanceRoot, const PrefabData& prefab);

	// What the prefab itself says about a field of a node (before any override); nullopt if it has no such node or the node has
	// no such component or field. This is the value a revert goes back to.
	std::optional<FieldValue> prefab_value(const PrefabData& prefab, u64 nodeId, std::string_view component, std::string_view field);

	// Removes the overrides that set that field of that node.
	void revert_field(EntityRecord& instanceRoot, u64 nodeId, std::string_view component, std::string_view field);
	void revert_all(EntityRecord& instanceRoot);

	// The expansion as plain entities: the root without its PrefabInstance and overrides, nothing linked to the prefab any more.
	std::vector<EntityRecord> unpack(const EntityRecord& instanceRoot, const PrefabData& prefab);
}
