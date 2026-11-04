#pragma once

#include <optional>
#include <vector>

#include "asset_guid.h"

namespace wiwa
{
	// Where cooked .wasset files come from: loose files in Library/ (editor) or the packed assets.wpak.
	// read() may be called from several worker threads at once.
	class IAssetSource
	{
	public:
		virtual ~IAssetSource() = default;

		// The whole .wasset file (header + payload), or nullopt if the asset is missing or damaged.
		virtual std::optional<std::vector<std::byte>> read(AssetGuid guid) = 0;
		virtual bool contains(AssetGuid guid) const = 0;

		// The catalog (which path names which asset), or nullopt if this source has none.
		virtual std::optional<std::vector<std::byte>> read_catalog() = 0;
	};
}
