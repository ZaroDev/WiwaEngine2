#include <pch.h>
#include "asset_catalog.h"

#include <algorithm>

namespace wiwa
{
	std::string AssetCatalog::normalize(std::string_view path)
	{
		std::string result(path);
		std::replace(result.begin(), result.end(), '\\', '/');
		while (result.starts_with("./"))
		{
			result.erase(0, 2);
		}
		return result;
	}

	std::optional<AssetRef> AssetCatalog::find(std::string_view path) const
	{
		const std::string wanted = normalize(path);
		for (const Entry& entry : Entries)
		{
			if (entry.Path == wanted)
			{
				return AssetRef{ entry.Guid, 0 };
			}
		}
		return std::nullopt;
	}

	void archive(BinaryArchive& ar, AssetCatalog& v)
	{
		ar.list(v.Entries, [&](AssetCatalog::Entry& entry)
			{
				ar.string(entry.Path);
				ar.value(entry.Guid);
			});
		if (ar.reading() && ar.remaining() == 0)
		{
			return;
		}
		ar.list(v.Redirects, [&](AssetCatalog::Redirect& redirect)
			{
				ar.value(redirect.From);
				ar.value(redirect.To);
			});
	}
}
