#pragma once

#include <optional>
#include <vector>

#include "asset_guid.h"
#include "asset_source.h"
#include "wasset_io.h"

namespace wiwa
{
	// The blobs a pack stores next to its assets (the catalog, the build info, the project's settings files). They are not
	// assets, so each lives under a reserved guid at the top of the guid range, which no generated guid reaches. All of
	// them are assigned here, so two can never collide; a new blob appends a number (never renumber: packs hold them).
	enum class ProjectBlob : u64
	{
		Catalog = 0,
		BuildInfo = 1,
		PhysicsSettings = 2,
		InputSettings = 3,
		ProjectSettings = 4,
		AudioSettings = 5,
		NavSettings = 6,
	};

	constexpr AssetGuid project_blob_guid(ProjectBlob blob)
	{
		return AssetGuid{ ~0ull, ~0ull - static_cast<u64>(blob) };
	}

	// One project blob of a source, deserialised as T (its archive() is the format); nullopt if the source has none
	// (a Library, an older pack) or it is damaged.
	template<typename T>
	std::optional<T> read_project_blob(IAssetSource& source, AssetGuid guid)
	{
		if (!source.contains(guid))
		{
			return std::nullopt;
		}

		std::optional<std::vector<std::byte>> bytes = source.read(guid);
		if (!bytes)
		{
			return std::nullopt;
		}
		return deserialize<T>(*bytes);
	}
}
