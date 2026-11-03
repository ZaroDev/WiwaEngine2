#pragma once

#include "action_system.h"

#include <nlohmann/json.hpp>

namespace wiwa
{
	constexpr u32 INPUT_OVERRIDES_VERSION = 1;

	// The player's rebinds: { "version": 1, "maps": { "Gameplay": { "Jump": [ binding, ... ] } } }. Only the actions whose bindings differ
	// from the defaults are stored, each with its complete binding list (this also covers added and removed bindings).
	nlohmann::json overrides_to_json(const ActionSystem& actions);

	// Applies the overrides to the actions and returns how many actions were changed. Anything that does not fit (an unknown map or action,
	// a binding the action cannot use, a control that does not exist, another version, a root that is not an object) is skipped and
	// described in `warnings`, one line each; the defaults of a skipped action stay. Never throws.
	size_t apply_overrides(ActionSystem& actions, const nlohmann::json& overrides, std::string& warnings);
}
