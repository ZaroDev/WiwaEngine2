#include <pch.h>
#include "bindings.h"

#include <scene/reflection/component_registry.h>

namespace wiwa
{
	template<typename V>
	void store_component(LuaVec<V>& vector, int component, float value)
	{
		vector.Value[component] = value;
		if (!vector.Link.linked() || vector.Link.Handle.Context == nullptr || vector.Link.Handle.Context->ActiveScene == nullptr)
		{
			return;
		}

		const entt::entity entity = vector.Link.Handle.resolve();
		const ComponentInfo* info = ComponentRegistry::get().find(vector.Link.Component);
		if (entity == entt::null || info == nullptr)
		{
			return;
		}

		entt::registry& registry = vector.Link.Handle.Context->ActiveScene->registry();
		FieldValue current = info->Get(registry, entity, vector.Link.Field);
		if (V* stored = std::get_if<V>(&current))
		{
			(*stored)[component] = value;
			info->Set(registry, entity, vector.Link.Field, current);
		}
	}

	template void store_component<glm::vec2>(LuaVec<glm::vec2>&, int, float);
	template void store_component<glm::vec3>(LuaVec<glm::vec3>&, int, float);
	template void store_component<glm::vec4>(LuaVec<glm::vec4>&, int, float);
	template void store_component<glm::quat>(LuaVec<glm::quat>&, int, float);

	namespace
	{
		template<typename V>
		LuaVec<V> value_of(const V& v) { return LuaVec<V>{ v, {} }; }

		// the parts every vector type shares: x/y/z/w access, arithmetic, comparison, length, dot, distance, lerp
		template<typename V, int N>
		sol::usertype<LuaVec<V>> bind_vector(sol::state& lua, const char* name)
		{
			using T = LuaVec<V>;
			sol::usertype<T> type = lua.new_usertype<T>(name);

			static const char* const parts[] = { "x", "y", "z", "w" };
			for (int i = 0; i < N; i++)
			{
				type[parts[i]] = sol::property(
					[i](const T& v) { return v.Value[i]; },
					[i](T& v, float x) { store_component<V>(v, i, x); });
			}

			type[sol::meta_function::addition] = [](const T& a, const T& b) { return value_of<V>(a.Value + b.Value); };
			type[sol::meta_function::subtraction] = [](const T& a, const T& b) { return value_of<V>(a.Value - b.Value); };
			type[sol::meta_function::unary_minus] = [](const T& a) { return value_of<V>(-a.Value); };
			type[sol::meta_function::multiplication] = sol::overload(
				[](const T& a, const T& b) { return value_of<V>(a.Value * b.Value); },
				[](const T& a, float s) { return value_of<V>(a.Value * s); },
				[](float s, const T& a) { return value_of<V>(s * a.Value); });
			type[sol::meta_function::division] = sol::overload(
				[](const T& a, const T& b) { return value_of<V>(a.Value / b.Value); },
				[](const T& a, float s) { return value_of<V>(a.Value / s); });
			type[sol::meta_function::equal_to] = [](const T& a, const T& b) { return a.Value == b.Value; };
			type[sol::meta_function::to_string] = [name](const T& a) { return std::string(name) + glm::to_string(a.Value); };

			type["length"] = [](const T& a) { return glm::length(a.Value); };
			type["normalize"] = [](const T& a) { return value_of<V>(glm::normalize(a.Value)); };
			type["dot"] = [](const T& a, const T& b) { return glm::dot(a.Value, b.Value); };
			type["distance"] = [](const T& a, const T& b) { return glm::distance(a.Value, b.Value); };
			type["lerp"] = [](const T& a, const T& b, float t) { return value_of<V>(glm::mix(a.Value, b.Value, t)); };
			return type;
		}

		void bind_quat(sol::state& lua)
		{
			sol::usertype<LuaQuat> type = lua.new_usertype<LuaQuat>("quat",
				sol::call_constructor, sol::factories(
					[]() { return value_of(glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f }); },
					[](float x, float y, float z, float w) { return value_of(glm::quat{ w, x, y, z }); }));

			static const char* const parts[] = { "x", "y", "z", "w" };
			for (int i = 0; i < 4; i++)
			{
				type[parts[i]] = sol::property(
					[i](const LuaQuat& q) { return q.Value[i]; },
					[i](LuaQuat& q, float x) { store_component<glm::quat>(q, i, x); });
			}

			type[sol::meta_function::multiplication] = sol::overload(
				[](const LuaQuat& a, const LuaQuat& b) { return value_of(a.Value * b.Value); },
				[](const LuaQuat& a, const LuaVec3& v) { return value_of(a.Value * v.Value); });
			type[sol::meta_function::equal_to] = [](const LuaQuat& a, const LuaQuat& b) { return a.Value == b.Value; };
			type[sol::meta_function::to_string] = [](const LuaQuat& a) { return "quat" + glm::to_string(a.Value); };

			type["inverse"] = [](const LuaQuat& a) { return value_of(glm::inverse(a.Value)); };
			type["normalize"] = [](const LuaQuat& a) { return value_of(glm::normalize(a.Value)); };
			type["to_euler"] = [](const LuaQuat& a) { return value_of(glm::degrees(glm::eulerAngles(a.Value))); };

			// degrees, applied in the order x, y, z
			type["from_euler"] = [](float x, float y, float z) { return value_of(glm::quat(glm::radians(glm::vec3(x, y, z)))); };
			type["from_axis_angle"] = [](const LuaVec3& axis, float degrees) { return value_of(glm::angleAxis(glm::radians(degrees), glm::normalize(axis.Value))); };
			type["identity"] = []() { return value_of(glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f }); };
		}

		void bind_mat4(sol::state& lua)
		{
			sol::usertype<LuaMat4> type = lua.new_usertype<LuaMat4>("mat4", sol::call_constructor, sol::factories([]() { return LuaMat4{}; }));

			type[sol::meta_function::multiplication] = [](const LuaMat4& a, const LuaMat4& b) { return LuaMat4{ a.Value * b.Value }; };
			type["inverse"] = [](const LuaMat4& a) { return LuaMat4{ glm::inverse(a.Value) }; };
			type["transform_point"] = [](const LuaMat4& m, const LuaVec3& p) { return value_of(glm::vec3(m.Value * glm::vec4(p.Value, 1.0f))); };
			type["transform_direction"] = [](const LuaMat4& m, const LuaVec3& d) { return value_of(glm::vec3(m.Value * glm::vec4(d.Value, 0.0f))); };

			type["identity"] = []() { return LuaMat4{}; };
			type["translate"] = [](const LuaVec3& v) { return LuaMat4{ glm::translate(glm::mat4(1.0f), v.Value) }; };
			type["scale"] = [](const LuaVec3& v) { return LuaMat4{ glm::scale(glm::mat4(1.0f), v.Value) }; };
			type["from_quat"] = [](const LuaQuat& q) { return LuaMat4{ glm::mat4_cast(q.Value) }; };
		}
	}

	void bind_math(ScriptRuntime& runtime)
	{
		sol::state& lua = runtime.state();

		sol::usertype<LuaVec2> vec2 = bind_vector<glm::vec2, 2>(lua, "vec2");
		vec2[sol::call_constructor] = sol::factories(
			[]() { return value_of(glm::vec2{ 0.0f }); },
			[](float s) { return value_of(glm::vec2{ s }); },
			[](float x, float y) { return value_of(glm::vec2{ x, y }); });

		sol::usertype<LuaVec3> vec3 = bind_vector<glm::vec3, 3>(lua, "vec3");
		vec3[sol::call_constructor] = sol::factories(
			[]() { return value_of(glm::vec3{ 0.0f }); },
			[](float s) { return value_of(glm::vec3{ s }); },
			[](float x, float y, float z) { return value_of(glm::vec3{ x, y, z }); });
		vec3["cross"] = [](const LuaVec3& a, const LuaVec3& b) { return value_of(glm::cross(a.Value, b.Value)); };

		sol::usertype<LuaVec4> vec4 = bind_vector<glm::vec4, 4>(lua, "vec4");
		vec4[sol::call_constructor] = sol::factories(
			[]() { return value_of(glm::vec4{ 0.0f }); },
			[](float s) { return value_of(glm::vec4{ s }); },
			[](float x, float y, float z, float w) { return value_of(glm::vec4{ x, y, z, w }); });

		bind_quat(lua);
		bind_mat4(lua);

		// the types are looked up by name from the sandbox
		for (const char* name : { "vec2", "vec3", "vec4", "quat", "mat4" })
		{
			runtime.sandbox()[name] = lua[name];
		}
	}
}
