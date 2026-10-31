#pragma once

#include <string>

#include <assets/asset_guid.h>
#include <math/math.h>
#include "../script_context.h"

// The C++ types Lua holds. They are values: copying one in Lua copies the number, except for the link a vector keeps to the field
// it was read from.
namespace wiwa
{
	// Where a vector was read from, so that `entity.transform.position.x = 4` writes the Transform. Empty for a vector that is
	// only a value (one made by vec3(...) or by arithmetic).
	struct FieldLink
	{
		EntityHandle Handle{};
		std::string Component{};
		size_t Field = 0;

		bool linked() const { return !Component.empty(); }
	};

	// Concrete types, not one template: sol2 looks for a to_string(x) by argument-dependent lookup, and a class that has a glm type as a
	// template argument would find glm::to_string, which does not accept it.
	struct LuaVec2 { glm::vec2 Value{}; FieldLink Link{}; };
	struct LuaVec3 { glm::vec3 Value{}; FieldLink Link{}; };
	struct LuaVec4 { glm::vec4 Value{}; FieldLink Link{}; };
	struct LuaQuat { glm::quat Value{ 1.0f, 0.0f, 0.0f, 0.0f }; FieldLink Link{}; };

	template<typename V> struct LuaVecOf;
	template<> struct LuaVecOf<glm::vec2> { using type = LuaVec2; };
	template<> struct LuaVecOf<glm::vec3> { using type = LuaVec3; };
	template<> struct LuaVecOf<glm::vec4> { using type = LuaVec4; };
	template<> struct LuaVecOf<glm::quat> { using type = LuaQuat; };

	// The Lua type that holds a vector of type V.
	template<typename V>
	using LuaVec = typename LuaVecOf<V>::type;

	struct LuaMat4
	{
		glm::mat4 Value{ 1.0f };
	};

	// An asset as a script holds it.
	struct LuaAsset
	{
		AssetRef Ref{};
	};

	// Writes one component of the vector the field holds (a read-modify-write, so the other components are not overwritten with
	// an old copy).
	template<typename V>
	void store_component(LuaVec<V>& vector, int component, float value); // call it with V spelled out

	// A component of the entity as scripts see it: `entity:get("Light")`. Reads and writes go to the field by name.
	struct ComponentProxy
	{
		EntityHandle Handle{};
		std::string Component{};
	};
}
