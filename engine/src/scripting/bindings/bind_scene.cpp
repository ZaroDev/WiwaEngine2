#include <pch.h>
#include "bindings.h"

namespace wiwa
{
	void bind_entity_handle(sol::state& lua)
	{
		lua.new_usertype<EntityHandle>("Entity", sol::no_constructor,
			"is_valid", &EntityHandle::is_valid,
			"guid", sol::property([](const EntityHandle& handle) { return handle.Guid.to_string(); }),
			sol::meta_function::equal_to, [](const EntityHandle& a, const EntityHandle& b) { return a.Guid == b.Guid; });
	}
}
