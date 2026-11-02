#include <pch.h>
#include "script_names.h"

namespace wiwa
{
	namespace
	{
		constexpr std::string_view PREFIX = "script:";
	}

	std::string script_component_name(AssetGuid guid)
	{
		return std::string(PREFIX) + guid.to_string();
	}

	std::optional<AssetGuid> script_guid_from_component(std::string_view name)
	{
		if (!name.starts_with(PREFIX))
		{
			return std::nullopt;
		}
		return AssetGuid::from_string(name.substr(PREFIX.size()));
	}
}
