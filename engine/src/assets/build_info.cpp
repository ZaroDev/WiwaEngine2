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
		return read_project_blob<BuildInfo>(source, BUILD_INFO_GUID);
	}
}
