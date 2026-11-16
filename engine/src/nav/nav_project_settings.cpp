#include <pch.h>
#include "nav_project_settings.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace wiwa
{
	namespace
	{
		std::string clean_name(const std::string& raw)
		{
			size_t first = 0;
			size_t last = raw.size();
			while (first < last && std::isspace(static_cast<unsigned char>(raw[first])))
			{
				first++;
			}
			while (last > first && std::isspace(static_cast<unsigned char>(raw[last - 1])))
			{
				last--;
			}
			std::string name = raw.substr(first, std::min(last - first, NAV_AREA_NAME_MAX));
			for (const char ch : name)
			{
				if (std::iscntrl(static_cast<unsigned char>(ch)))
				{
					return {};
				}
			}
			return name;
		}
	}

	NavSettings sanitized(NavSettings settings)
	{
		settings.AreaName[0] = "NotWalkable";
		settings.AreaName[1] = "Walkable";
		for (u32 i = 2; i < NAV_AREA_COUNT; i++)
		{
			settings.AreaName[i] = clean_name(settings.AreaName[i]);
			for (u32 other = 0; other < i && !settings.AreaName[i].empty(); other++)
			{
				if (settings.AreaName[other] == settings.AreaName[i])
				{
					settings.AreaName[i].clear();
				}
			}
		}
		for (u32 i = 0; i < NAV_AREA_COUNT; i++)
		{
			float& cost = settings.AreaCost[i];
			cost = !settings.used(i) ? 1.0f : (std::isfinite(cost) ? std::clamp(cost, 1.0f, NAV_AREA_COST_MAX) : 1.0f);
		}
		return settings;
	}

	bool nav_area_name_ok(const NavSettings& settings, u32 id, const std::string& name)
	{
		if (id < 2 || id >= NAV_AREA_COUNT || name.empty() || clean_name(name) != name)
		{
			return false;
		}
		for (u32 i = 0; i < NAV_AREA_COUNT; i++)
		{
			if (i != id && settings.used(i) && settings.AreaName[i] == name)
			{
				return false;
			}
		}
		return true;
	}

	void archive(BinaryArchive& ar, NavSettings& v)
	{
		u32 version = NAV_SETTINGS_VERSION;
		ar.value(version);
		if (ar.reading() && ar.ok() && version != NAV_SETTINGS_VERSION && version != 1)
		{
			ar.fail();
			return;
		}

		if (ar.reading() && version == 1)
		{
			// the eight fixed areas, as they were: Custom0..3 become areas of that name only when they had a cost
			std::array<float, 8> old{};
			ar.value(old);
			if (!ar.ok())
			{
				return;
			}
			NavSettings settings;
			for (u32 i = 0; i < 8; i++)
			{
				if (!std::isfinite(old[i]) || old[i] < 1.0f)
				{
					ar.fail();
					return;
				}
				settings.AreaCost[i] = old[i];
				if (i >= 4)
				{
					settings.AreaName[i] = old[i] > 1.0f ? "Custom" + std::to_string(i - 4) : std::string();
				}
			}
			if (ar.remaining() != 0)
			{
				ar.fail();
				return;
			}
			v = settings;
			return;
		}

		ar.value(v.AreaCost);
		for (std::string& name : v.AreaName)
		{
			ar.string(name);
		}
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
			if (v.AreaName[0] != "NotWalkable" || v.AreaName[1] != "Walkable")
			{
				ar.fail();
				return;
			}
			for (u32 i = 2; i < NAV_AREA_COUNT; i++)
			{
				if (v.AreaName[i] != clean_name(v.AreaName[i]) || (!v.AreaName[i].empty() && nav_area_id(v, v.AreaName[i]) != static_cast<int>(i)))
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
