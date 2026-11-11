#include <pch.h>
#include "nav_project_settings.h"

#include <algorithm>
#include <cmath>

namespace wiwa
{
	NavSettings sanitized(NavSettings settings)
	{
		for (float& cost : settings.AreaCost)
		{
			cost = std::isfinite(cost) ? std::clamp(cost, 1.0f, NAV_AREA_COST_MAX) : 1.0f;
		}
		return settings;
	}

	void archive(BinaryArchive& ar, NavSettings& v)
	{
		u32 version = NAV_SETTINGS_VERSION;
		ar.value(version);
		if (ar.reading() && ar.ok() && version != NAV_SETTINGS_VERSION)
		{
			ar.fail();
			return;
		}

		ar.value(v.AreaCost);
		if (ar.reading() && ar.ok())
		{
			for (const float cost : v.AreaCost)
			{
				if (!std::isfinite(cost) || cost < 1.0f)
				{
					ar.fail();
					return;
				}
			}
			if (ar.remaining() != 0)
			{
				ar.fail();
			}
		}
	}

	std::optional<NavSettings> read_nav_settings(IAssetSource& source)
	{
		if (!source.contains(NAV_SETTINGS_GUID))
		{
			return std::nullopt;
		}

		std::optional<std::vector<std::byte>> bytes = source.read(NAV_SETTINGS_GUID);
		if (!bytes)
		{
			return std::nullopt;
		}
		return deserialize<NavSettings>(*bytes);
	}
}
