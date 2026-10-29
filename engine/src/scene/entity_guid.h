#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <math/math.h>

namespace wiwa
{
	// 128-bit identifier of an entity inside a scene file. It is stable across save and load; the entities a prefab instance
	// generates get theirs derived from the instance, so they are the same on every run.
	struct EntityGuid
	{
		u64 Hi = 0;
		u64 Lo = 0;

		static EntityGuid generate();
		// A guid that depends only on the two inputs.
		static EntityGuid derive(EntityGuid parent, u64 key);
		static std::optional<EntityGuid> from_string(std::string_view text);

		// 32 lowercase hex characters
		std::string to_string() const;

		bool valid() const { return Hi != 0 || Lo != 0; }

		bool operator==(const EntityGuid& other) const { return Hi == other.Hi && Lo == other.Lo; }
		bool operator!=(const EntityGuid& other) const { return !(*this == other); }
		bool operator<(const EntityGuid& other) const { return Hi != other.Hi ? Hi < other.Hi : Lo < other.Lo; }
	};
}

template<>
struct std::hash<wiwa::EntityGuid>
{
	size_t operator()(const wiwa::EntityGuid& guid) const noexcept
	{
		return static_cast<size_t>(guid.Hi ^ (guid.Lo * 0x9E3779B97F4A7C15ull));
	}
};
