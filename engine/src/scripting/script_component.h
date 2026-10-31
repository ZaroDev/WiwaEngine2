#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <entt/entt.hpp>

#include <assets/asset_guid.h>
#include "script_names.h"
#include "script_runtime.h"

namespace wiwa
{
	// Registers (or registers again, with new fields) the value-bag component of a script class in the ComponentRegistry. With a
	// registry, the entities that already hold the component keep the values whose name and type still match.
	void register_script_component(const ScriptClass& scriptClass, entt::registry* reconcile = nullptr);
}
