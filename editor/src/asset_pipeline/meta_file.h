#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "assets/asset_guid.h"

namespace wiwa
{
	// The <file>.meta sidecar of a source asset: its guid, which importer cooks it, the import settings, and the stable
	// local id of every sub-asset the importer produced (by name), so re-importing never changes a guid.
	struct MetaFile
	{
		AssetGuid Guid{};
		std::string Importer{};
		u32 Version = 0; // version of the importer that produced the Library files
		nlohmann::json Settings = nlohmann::json::object();
		std::map<std::string, u64> SubAssets{};

		static std::filesystem::path meta_path_for(const std::filesystem::path& source);

		// nullopt if the file is missing or cannot be parsed
		static std::optional<MetaFile> load(const std::filesystem::path& metaPath);
		bool save(const std::filesystem::path& metaPath) const;
	};
}
