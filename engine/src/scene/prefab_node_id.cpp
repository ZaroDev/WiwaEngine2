#include <pch.h>
#include "prefab_node_id.h"

#include <unordered_set>

namespace wiwa
{
	u64 compute_node_id(std::string_view namePath, u32 siblingIndex)
	{
		u64 hash = 0xCBF29CE484222325ull;
		auto mix = [&](std::string_view text)
		{
			for (const char c : text)
			{
				hash ^= static_cast<u8>(c);
				hash *= 0x100000001B3ull;
			}
		};

		mix(namePath);
		if (siblingIndex > 0)
		{
			mix("#");
			mix(std::to_string(siblingIndex));
		}
		return hash == 0 ? 1 : hash;
	}

	void assign_stable_ids(std::vector<PrefabNode>& nodes)
	{
		const size_t count = nodes.size();

		// the segment of each node: its name, plus "#n" when n earlier siblings have the same name
		std::vector<std::string> segment(count);
		std::vector<u32> siblingIndex(count, 0);
		std::unordered_map<std::string, u32> seen;
		for (size_t i = 0; i < count; i++)
		{
			const std::string key = std::to_string(nodes[i].Parent) + "/" + nodes[i].Name;
			siblingIndex[i] = seen[key]++;
			segment[i] = siblingIndex[i] > 0 ? nodes[i].Name + "#" + std::to_string(siblingIndex[i]) : nodes[i].Name;
		}

		std::unordered_set<u64> used;
		for (size_t i = 0; i < count; i++)
		{
			// walk up to the root; a chain longer than the node count is a loop, cut there
			std::vector<size_t> chain;
			i32 current = static_cast<i32>(i);
			while (current >= 0 && current < static_cast<i32>(count) && chain.size() <= count)
			{
				chain.push_back(static_cast<size_t>(current));
				current = nodes[current].Parent;
			}

			// the path of the parent, then the node's own name (its sibling index goes into the hash separately)
			std::string path;
			for (size_t k = chain.size(); k-- > 1;)
			{
				path += segment[chain[k]];
				path += '/';
			}
			path += nodes[i].Name;

			u64 id = compute_node_id(path, siblingIndex[i]);
			while (used.contains(id))
			{
				id = (id == ~0ull) ? 1 : id + 1;
			}
			used.insert(id);
			nodes[i].StableId = id;
		}
	}
}
