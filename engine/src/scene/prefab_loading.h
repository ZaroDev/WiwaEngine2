#pragma once

#include <unordered_set>
#include <vector>

#include <assets/asset_data.h>
#include <assets/handle.h>

#include "prefab_expander.h"

namespace wiwa
{
	// Whether the prefabs that `root` instantiates (directly, or through the ones those instantiate) have loaded. handleOf(AssetRef) gives the
	// Handle<PrefabData> of a prefab; the caller keeps those handles, so the prefabs stay loaded. Loading as soon as one is still loading;
	// a prefab that failed counts as done (the expansion leaves its instances out). Each prefab is visited once, so a cycle ends.
	template<typename HandleOf>
	LoadState nested_prefabs_state(const PrefabData& root, HandleOf&& handleOf)
	{
		std::unordered_set<AssetGuid> seen;
		std::vector<const PrefabData*> work{ &root };
		std::vector<AssetRef> refs;
		while (!work.empty())
		{
			const PrefabData* prefab = work.back();
			work.pop_back();

			refs.clear();
			collect_nested_prefabs(*prefab, refs);
			for (const AssetRef& ref : refs)
			{
				if (!seen.insert(ref.resolved()).second)
				{
					continue;
				}

				Handle<PrefabData> handle = handleOf(ref);
				const LoadState state = handle.state();
				if (state == LoadState::Loading)
				{
					return LoadState::Loading;
				}
				if (state == LoadState::Ready)
				{
					work.push_back(handle.get());
				}
			}
		}
		return LoadState::Ready;
	}
}
