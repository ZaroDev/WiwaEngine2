#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>
#include "field_descriptor.h"

namespace wiwa
{
	// One component as the serializer, the inspector and the overrides see it. The functions hide how the values are stored
	// (a C++ struct, or the value bag of a script), so generic code never needs the C++ type.
	struct ComponentInfo
	{
		std::string Name{};
		std::vector<FieldDescriptor> Fields{};

		std::function<bool(entt::registry&, entt::entity)> Has{};
		std::function<void(entt::registry&, entt::entity)> Add{};    // with default values; does nothing if it is there
		std::function<void(entt::registry&, entt::entity)> Remove{};

		// The default value if the entity lacks the component or the index is out of range.
		std::function<FieldValue(entt::registry&, entt::entity, size_t)> Get{};
		// False, and nothing changes, if the entity lacks the component, the index is out of range, or the value is not valid
		// for the field.
		std::function<bool(entt::registry&, entt::entity, size_t, const FieldValue&)> Set{};

		// -1 if there is no such field.
		int field_index(std::string_view name) const
		{
			for (size_t i = 0; i < Fields.size(); i++)
			{
				if (Fields[i].Name == name)
				{
					return static_cast<int>(i);
				}
			}
			return -1;
		}
	};
}
