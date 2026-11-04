#pragma once

#include "action_types.h"

#include <nlohmann/json.hpp>
#include <optional>

namespace wiwa
{
	constexpr u32 INPUT_MAP_VERSION = 1;

	// The .winput source format (JSON). On failure the output is left untouched and `error` names the offender.
	bool input_map_from_text(const std::string& text, InputMapData& out, std::string& error);
	std::string input_map_to_text(const InputMapData& data);

	// One binding, as it appears in the .winput file and in the override file. Scale, deadzone and invert are written only when
	// they differ from the defaults.
	nlohmann::json binding_to_json(const Binding& binding);
	std::optional<Binding> binding_from_json(const nlohmann::json& json, std::string& error);
}
