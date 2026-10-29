#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "component_info.h"

namespace wiwa
{
	// The values of one run-time-defined component (a script's properties), by field name.
	struct ValueBag
	{
		std::string Name{};
		std::unordered_map<std::string, FieldValue> Values{};
	};

	// Every value-bag component of an entity. One EnTT type holds them all, so any number of scripts can be attached.
	struct ValueBags
	{
		std::vector<ValueBag> Bags{};
	};

	// A ComponentInfo for a component that has no C++ struct: its fields exist only at run time. Register it with the
	// ComponentRegistry like any other.
	ComponentInfo make_value_bag_info(std::string name, std::vector<FieldDescriptor> fields);

	// After a value-bag component was registered again with other fields: every entity that has it keeps the values whose name
	// and type still match, and takes the default for the rest.
	void reconcile_bag(entt::registry& registry, const ComponentInfo& info);
}
