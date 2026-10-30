#include <pch.h>
#include "build_info.h"

namespace wiwa
{
	void archive(BinaryArchive& ar, BuildInfo& v)
	{
		ar.value(v.DefaultScene);
	}

	std::optional<BuildInfo> read_build_info(IAssetSource& source)
	{
		if (!source.contains(BUILD_INFO_GUID))
		{
			return std::nullopt;
		}

		std::optional<std::vector<std::byte>> bytes = source.read(BUILD_INFO_GUID);
		if (!bytes)
		{
			return std::nullopt;
		}
		return deserialize<BuildInfo>(*bytes);
	}
}
