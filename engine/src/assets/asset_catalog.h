#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "asset_guid.h"
#include "wasset_io.h"

namespace wiwa
{
	// The file name of the catalog in the Library folder.
	constexpr const char* CATALOG_FILE_NAME = "catalog.wcat";

	// The key a pack stores its catalog under (it is not an asset, so no real guid is this one).
	constexpr AssetGuid CATALOG_GUID{ ~0ull, ~0ull };

	// Which asset a source path names: "Assets/models/torus_knot.glb", "engine/shaders/imgui.hlsl". Written by the editor's
	// asset database next to the cooked files and packed with them, so code (and a game) can refer to an asset by the path
	// it has in the project instead of by guid. Only whole assets are listed; sub-assets are reached through their parent.
	struct AssetCatalog
	{
		struct Entry
		{
			std::string Path{};
			AssetGuid Guid{};
		};

		std::vector<Entry> Entries{};

		// nullopt if the path is not in the catalog. Slashes either way, and a leading "./", are accepted.
		std::optional<AssetRef> find(std::string_view path) const;

		static std::string normalize(std::string_view path);
	};

	void archive(BinaryArchive& ar, AssetCatalog& v);
}
