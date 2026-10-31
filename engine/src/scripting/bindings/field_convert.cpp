#include <pch.h>
#include "bindings.h"

namespace wiwa
{
	namespace
	{
		template<typename V>
		sol::object vector_to_lua(sol::state_view lua, const V& value, const FieldLink& link)
		{
			return sol::make_object(lua, LuaVec<V>{ value, link });
		}

		template<typename V>
		bool vector_from_lua(const sol::object& object, FieldValue& out)
		{
			if (!object.is<LuaVec<V>>())
			{
				return false;
			}
			out = object.as<const LuaVec<V>&>().Value;
			return true;
		}

		std::string lua_type_of(const sol::object& object)
		{
			return sol::type_name(object.lua_state(), object.get_type());
		}
	}

	sol::object field_to_lua(sol::state_view lua, const FieldValue& value, const FieldDescriptor& field, const FieldLink& link)
	{
		switch (field.Type)
		{
		case FieldType::Bool:
			return sol::make_object(lua, std::get<bool>(value));
		case FieldType::Int:
		case FieldType::Enum:
			return sol::make_object(lua, std::get<i32>(value));
		case FieldType::Float:
			return sol::make_object(lua, std::get<float>(value));
		case FieldType::Vec2:
			return vector_to_lua(lua, std::get<glm::vec2>(value), link);
		case FieldType::Vec3:
			return vector_to_lua(lua, std::get<glm::vec3>(value), link);
		case FieldType::Vec4:
		case FieldType::Color:
			return vector_to_lua(lua, std::get<glm::vec4>(value), link);
		case FieldType::Quat:
			return vector_to_lua(lua, std::get<glm::quat>(value), link);
		case FieldType::String:
			return sol::make_object(lua, std::get<std::string>(value));
		case FieldType::Asset:
		{
			const AssetRef& ref = std::get<AssetRef>(value);
			return ref.valid() ? sol::make_object(lua, LuaAsset{ ref }) : sol::object(lua, sol::lua_nil);
		}
		case FieldType::AssetList:
		{
			sol::table list = sol::table::create(lua.lua_state());
			for (const AssetRef& ref : std::get<std::vector<AssetRef>>(value))
			{
				list.add(LuaAsset{ ref });
			}
			return list;
		}
		case FieldType::Entity:
		{
			// the context is the one of the link's handle; a field of an entity that is gone has no link, so the handle stays unresolved
			const EntityGuid& guid = std::get<EntityGuid>(value);
			return guid.valid() ? sol::make_object(lua, EntityHandle{ guid, link.Handle.Context }) : sol::object(lua, sol::lua_nil);
		}
		}
		return sol::object(lua, sol::lua_nil);
	}

	bool field_from_lua(const FieldDescriptor& field, const sol::object& object, FieldValue& out, std::string& error)
	{
		bool ok = false;
		switch (field.Type)
		{
		case FieldType::Bool:
			if (ok = object.is<bool>()) out = object.as<bool>();
			break;
		case FieldType::Int:
			if (ok = object.is<double>()) out = static_cast<i32>(object.as<double>());
			break;
		case FieldType::Float:
			if (ok = object.is<double>()) out = static_cast<float>(object.as<double>());
			break;
		case FieldType::Vec2:
			ok = vector_from_lua<glm::vec2>(object, out);
			break;
		case FieldType::Vec3:
			ok = vector_from_lua<glm::vec3>(object, out);
			break;
		case FieldType::Vec4:
		case FieldType::Color:
			ok = vector_from_lua<glm::vec4>(object, out);
			break;
		case FieldType::Quat:
			ok = vector_from_lua<glm::quat>(object, out);
			break;
		case FieldType::String:
			if (ok = object.is<std::string>()) out = object.as<std::string>();
			break;
		case FieldType::Enum:
			if (object.is<double>())
			{
				out = static_cast<i32>(object.as<double>());
				ok = is_valid_field_value(field, out);
			}
			else if (object.is<std::string>())
			{
				const auto found = std::find(field.Meta.EnumNames.begin(), field.Meta.EnumNames.end(), object.as<std::string>());
				if (ok = found != field.Meta.EnumNames.end()) out = static_cast<i32>(found - field.Meta.EnumNames.begin());
			}
			break;
		case FieldType::Asset:
			if (object.get_type() == sol::type::lua_nil)
			{
				out = AssetRef{};
				ok = true;
			}
			else if (ok = object.is<LuaAsset>())
			{
				out = object.as<const LuaAsset&>().Ref;
			}
			break;
		case FieldType::AssetList:
			if (object.is<sol::table>())
			{
				std::vector<AssetRef> list;
				ok = true;
				for (const auto& item : object.as<sol::table>())
				{
					if (!item.second.is<LuaAsset>())
					{
						ok = false;
						break;
					}
					list.push_back(item.second.as<const LuaAsset&>().Ref);
				}
				if (ok) out = std::move(list);
			}
			break;
		case FieldType::Entity:
			if (object.get_type() == sol::type::lua_nil)
			{
				out = EntityGuid{};
				ok = true;
			}
			else if (ok = object.is<EntityHandle>())
			{
				out = object.as<const EntityHandle&>().Guid;
			}
			break;
		}

		if (!ok)
		{
			error = std::string("expected ") + field_type_name(field.Type) + ", got " + lua_type_of(object);
		}
		return ok;
	}
}
