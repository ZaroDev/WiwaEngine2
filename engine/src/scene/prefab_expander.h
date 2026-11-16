#pragma once

#include <functional>
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

	// How many levels of prefab instances inside prefab instances are expanded; a deeper instance is left out with a warning.
	constexpr u32 MAX_PREFAB_DEPTH = 8;

	// Returns the prefab an AssetRef names, or null if it is not available (missing, still loading, or it failed to load). The
	// expansion only reads what it returns, so it can run on a worker thread over immutable data.
	using PrefabResolver = std::function<const PrefabData*(const AssetRef&)>;

	struct ExpandResult
	{
		std::vector<EntityRecord> Entities{}; // the instance root first, then every entity of the instance, parents before children
		std::vector<u64> NodeIds{};           // parallel to Entities: 0 for the root, the prefab node's id (a nested instance's nodes have compose_node_id ids), or ADDED_CHILD_NODE
		std::vector<u8> NestedRoots{};        // parallel to Entities: 1 for the node that is the root of a nested prefab instance
		std::vector<std::string> Warnings{};
		std::vector<u64> DroppedOverrideNodes{}; // nodes the overrides name that the prefab does not have (any more)
	};

	// instanceRoot is the record of the entity that holds the PrefabInstance. Its own record comes back first, with its overrides
	// kept (so the scene can be saved again); the node entities get the id derived from the root's id and the node's StableId.
	// Overrides are applied in order. An override on a node the prefab does not have is dropped with a warning, never applied
	// to another node.
	//
	// A node that holds a nested instance (PrefabNode::Instance) is followed into the prefab the resolver gives for it: the inner nodes
	// become children of that node, with ids compose_node_id(the node's id, the inner node's id), and the overrides stored on the
	// instance are applied first (their node ids rewritten onto those composite ids), then the root's. An instance whose prefab is not
	// available, that would make a cycle, or that is deeper than MAX_PREFAB_DEPTH stays a plain entity and adds a warning.
	ExpandResult expand_prefab(const EntityRecord& instanceRoot, const PrefabData& prefab, const PrefabResolver& resolve);
	// Without a resolver every nested instance counts as not available.
	ExpandResult expand_prefab(const EntityRecord& instanceRoot, const PrefabData& prefab);

	// The prefab refs that the nodes of this prefab instantiate (not followed any further).
	void collect_nested_prefabs(const PrefabData& prefab, std::vector<AssetRef>& out);

	// What the prefab itself says about a field of a node (before any override of a scene); nullopt if it has no such node or the node
	// has no such component or field. This is the value a revert goes back to. With a resolver the nodes of nested instances are found
	// too (nodeId is then the composite id), with the overrides of the instances applied.
	std::optional<FieldValue> prefab_value(const PrefabData& prefab, u64 nodeId, std::string_view component, std::string_view field, const PrefabResolver& resolve);
	std::optional<FieldValue> prefab_value(const PrefabData& prefab, u64 nodeId, std::string_view component, std::string_view field);

	// Whether the override sets that field of that node (the match every lookup, replace and revert of a field override uses).
	bool is_field_override(const PrefabOverride& override, u64 nodeId, std::string_view component, std::string_view field);

	// Removes the overrides that set that field of that node.
	void revert_field(EntityRecord& instanceRoot, u64 nodeId, std::string_view component, std::string_view field);
	void revert_all(EntityRecord& instanceRoot);

	// The expansion as plain entities: the root without its PrefabInstance and overrides, nothing linked to the prefab any more.
	std::vector<EntityRecord> unpack(const EntityRecord& instanceRoot, const PrefabData& prefab, const PrefabResolver& resolve);
	std::vector<EntityRecord> unpack(const EntityRecord& instanceRoot, const PrefabData& prefab);
}
