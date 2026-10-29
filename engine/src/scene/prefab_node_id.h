#pragma once

#include <string_view>
#include <vector>

#include <assets/asset_data.h>

namespace wiwa
{
	// The id of a node that does not change when the model is imported again: FNV-1a 64 of the path of names from the root
	// ("Body/Wheel"), with "#<n>" added for the n-th sibling (n >= 1) that has the same name as an earlier one. Never 0.
	u64 compute_node_id(std::string_view namePath, u32 siblingIndex);

	// Gives every node its StableId (replacing what was there). Ids are unique within the list; a parent loop is tolerated.
	// An id depends only on the node's names along its path, so adding a node leaves the other ids as they were, and renaming
	// a node changes the ids below it.
	void assign_stable_ids(std::vector<PrefabNode>& nodes);
}
