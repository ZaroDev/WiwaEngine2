#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <assets/asset_guid.h>

namespace wiwa
{
	// A script is a component named after its asset: "script:<guid>". The name is what scene files and the registry use; the
	// ComponentInfo's DisplayName (the file name) is what the editor shows.
	std::string script_component_name(AssetGuid guid);
	std::optional<AssetGuid> script_guid_from_component(std::string_view name);
}
