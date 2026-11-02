#pragma once

#include <string>
#include <vector>

#include <sol/sol.hpp>

#include <scene/reflection/field_descriptor.h>

namespace wiwa
{
	// Reads the `properties` table of a script class into field descriptors, sorted by name (a Lua table has no order):
	//   speed = { type = "float", default = 5, min = 0, max = 20 }
	// type is one of bool, int, float, vec2, vec3, vec4, color, quat, string, enum (with `names`), asset (with `asset` = the asset
	// type name, any case), asset_list, entity. A missing default is the type's zero value. On a malformed entry `error` says
	// which property and why, and the result is empty.
	std::vector<FieldDescriptor> parse_properties(const sol::table& properties, std::string& error);
}
