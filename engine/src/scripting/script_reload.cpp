#include <pch.h>
#include "script_reload.h"

#include <algorithm>

namespace wiwa
{
	bool is_script_asset(std::span<const AssetCatalog::Entry> catalog, AssetGuid guid)
	{
		return std::any_of(catalog.begin(), catalog.end(), [&](const AssetCatalog::Entry& entry)
			{
				return entry.Guid == guid && entry.Path.ends_with(".lua");
			});
	}
}
