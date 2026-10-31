#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <assets/asset_guid.h>

namespace wiwa
{
	// A script as the runtime reads it: which asset, where it came from (the catalog path, lowercase), and its source.
	struct LoadedScript
	{
		AssetRef Asset{};
		std::string Path{};
		std::string Source{};
	};

	// Where the runtime gets scripts from. The game reads them through the AssetManager; tests and the importer supply them from memory.
	class IScriptSource
	{
	public:
		virtual ~IScriptSource() = default;

		virtual std::optional<LoadedScript> find_by_path(std::string_view path) = 0;
		virtual std::optional<LoadedScript> find_by_asset(AssetRef ref) = 0;
	};
}
