#pragma once

#include <optional>

#include "asset_guid.h"
#include "asset_source.h"
#include "project_blob.h"
#include "wasset_io.h"

namespace wiwa
{
	// The key a pack stores its build info under (it is not an asset, so no real guid is this one).
	constexpr AssetGuid BUILD_INFO_GUID = project_blob_guid(ProjectBlob::BuildInfo);

	// What the editor decided when it packed the project: which scene a game starts with when it is given no --scene.
	struct BuildInfo
	{
		AssetGuid DefaultScene{};
	};

	void archive(BinaryArchive& ar, BuildInfo& v);

	// The build info of a source; nullopt if the source has none (a Library, an older pack) or it is damaged.
	std::optional<BuildInfo> read_build_info(IAssetSource& source);
}
