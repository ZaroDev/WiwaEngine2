#pragma once

#include <sol/sol.hpp>

#include "../script_context.h"

// One function per area of the Lua API; each registers its part in the state of a ScriptRuntime.
namespace wiwa
{
	// The Entity type scripts hold.
	void bind_entity_handle(sol::state& lua);
}
