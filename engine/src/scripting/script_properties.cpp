#include <pch.h>
#include "script_properties.h"

#include <algorithm>
#include <cctype>

namespace wiwa
{
	namespace
	{
		bool equals_ignore_case(const std::string& a, const std::string& b)
		{
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y)
				{
					return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
				});
		}

		// A vector as a table: { x, y, z } or { x = , y = , z = } (and r, g, b, a for a color).
		bool read_components(const sol::object& object, float* out, int count)
		{
			if (!object.is<sol::table>())
			{
				return false;
			}

			sol::table table = object.as<sol::table>();
			static const char* const xyzw[] = { "x", "y", "z", "w" };
			static const char* const rgba[] = { "r", "g", "b", "a" };
			for (int i = 0; i < count; i++)
			{
				sol::object value = table[i + 1];
				if (!value.is<double>())
				{
					value = table[xyzw[i]];
				}
				if (!value.is<double>())
				{
					value = table[rgba[i]];
				}
				if (!value.is<double>())
				{
					return false;
				}
				out[i] = static_cast<float>(value.as<double>());
			}
			return true;
		}

		// A vector the engine bound to Lua, or a table of numbers.
		template<typename V>
		bool read_vector(const sol::object& object, V& out, int count)
		{
			if (object.is<V>())
			{
				out = object.as<V>();
				return true;
			}
			float values[4] = {};
			if (!read_components(object, values, count))
			{
				return false;
			}
			for (int i = 0; i < count; i++)
			{
				out[i] = values[i];
			}
			return true;
		}

		bool parse_type(const std::string& name, FieldType& type)
		{
			static const std::pair<const char*, FieldType> types[] = {
				{ "bool", FieldType::Bool }, { "int", FieldType::Int }, { "float", FieldType::Float },
				{ "vec2", FieldType::Vec2 }, { "vec3", FieldType::Vec3 }, { "vec4", FieldType::Vec4 },
				{ "color", FieldType::Color }, { "quat", FieldType::Quat }, { "string", FieldType::String },
				{ "enum", FieldType::Enum }, { "asset", FieldType::Asset }, { "asset_list", FieldType::AssetList },
				{ "entity", FieldType::Entity },
			};
			for (const auto& [text, value] : types)
			{
				if (name == text)
				{
					type = value;
					return true;
				}
			}
			return false;
		}

		FieldValue zero_value(FieldType type)
		{
			switch (type)
			{
			case FieldType::Bool: return false;
			case FieldType::Int:
			case FieldType::Enum: return i32{ 0 };
			case FieldType::Float: return 0.0f;
			case FieldType::Vec2: return glm::vec2{ 0.0f };
			case FieldType::Vec3: return glm::vec3{ 0.0f };
			case FieldType::Vec4:
			case FieldType::Color: return glm::vec4{ 0.0f };
			case FieldType::Quat: return glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f };
			case FieldType::String: return std::string{};
			case FieldType::Asset: return AssetRef{};
			case FieldType::AssetList: return std::vector<AssetRef>{};
			case FieldType::Entity: return EntityGuid{};
			}
			return false;
		}

		// The default as written in the script; false if it is not a value of the type.
		bool read_default(const sol::object& object, FieldDescriptor& field)
		{
			switch (field.Type)
			{
			case FieldType::Bool:
				if (!object.is<bool>()) return false;
				field.Default = object.as<bool>();
				return true;
			case FieldType::Int:
				if (!object.is<double>()) return false;
				field.Default = static_cast<i32>(object.as<double>());
				return true;
			case FieldType::Float:
				if (!object.is<double>()) return false;
				field.Default = static_cast<float>(object.as<double>());
				return true;
			case FieldType::Vec2:
			{
				glm::vec2 v{};
				if (!read_vector(object, v, 2)) return false;
				field.Default = v;
				return true;
			}
			case FieldType::Vec3:
			{
				glm::vec3 v{};
				if (!read_vector(object, v, 3)) return false;
				field.Default = v;
				return true;
			}
			case FieldType::Vec4:
			case FieldType::Color:
			{
				glm::vec4 v{};
				if (!read_vector(object, v, 4)) return false;
				field.Default = v;
				return true;
			}
			case FieldType::Quat:
			{
				if (object.is<glm::quat>())
				{
					field.Default = object.as<glm::quat>();
					return true;
				}
				float v[4] = {};
				if (!read_components(object, v, 4)) return false;
				field.Default = glm::quat{ v[3], v[0], v[1], v[2] }; // written as x, y, z, w
				return true;
			}
			case FieldType::String:
				if (!object.is<std::string>()) return false;
				field.Default = object.as<std::string>();
				return true;
			case FieldType::Enum:
			{
				if (object.is<std::string>())
				{
					const auto& names = field.Meta.EnumNames;
					const auto found = std::find(names.begin(), names.end(), object.as<std::string>());
					if (found == names.end()) return false;
					field.Default = static_cast<i32>(found - names.begin());
					return true;
				}
				if (!object.is<double>()) return false;
				field.Default = static_cast<i32>(object.as<double>());
				return is_valid_field_value(field, field.Default);
			}
			case FieldType::Asset:
			case FieldType::AssetList:
			case FieldType::Entity:
				return false; // only "no value" is a default for these
			}
			return false;
		}
	}

	std::vector<FieldDescriptor> parse_properties(const sol::table& properties, std::string& error)
	{
		std::vector<FieldDescriptor> fields;
		error.clear();

		for (const auto& [key, value] : properties)
		{
			if (!key.is<std::string>() || !value.is<sol::table>())
			{
				error = "properties must be a table of name = { type = ... }";
				return {};
			}

			const std::string name = key.as<std::string>();
			const sol::table entry = value.as<sol::table>();

			FieldDescriptor field;
			field.Name = name;

			const sol::optional<std::string> typeName = entry["type"];
			if (!typeName)
			{
				error = "property '" + name + "' has no type";
				return {};
			}
			if (!parse_type(*typeName, field.Type))
			{
				error = "property '" + name + "' has an unknown type '" + *typeName + "'";
				return {};
			}
			field.Default = zero_value(field.Type);

			if (field.Type == FieldType::Float || field.Type == FieldType::Int)
			{
				const sol::optional<double> minimum = entry["min"];
				const sol::optional<double> maximum = entry["max"];
				if (minimum && maximum)
				{
					field.Meta = FieldMeta::range(static_cast<float>(*minimum), static_cast<float>(*maximum));
				}
			}
			else if (field.Type == FieldType::Color)
			{
				field.Meta = FieldMeta::color();
			}
			else if (field.Type == FieldType::Enum)
			{
				const sol::optional<sol::table> names = entry["names"];
				if (!names)
				{
					error = "property '" + name + "' is an enum and needs `names`";
					return {};
				}
				for (size_t i = 1; i <= names->size(); i++)
				{
					const sol::optional<std::string> label = (*names)[i];
					if (!label)
					{
						error = "property '" + name + "': `names` must be strings";
						return {};
					}
					field.Meta.EnumNames.push_back(*label);
				}
				if (field.Meta.EnumNames.empty())
				{
					error = "property '" + name + "': `names` is empty";
					return {};
				}
			}
			else if (field.Type == FieldType::Asset || field.Type == FieldType::AssetList)
			{
				const sol::optional<std::string> assetName = entry["asset"];
				if (!assetName)
				{
					error = "property '" + name + "' holds assets and needs `asset` (the asset type)";
					return {};
				}
				for (u32 type = 1; type < static_cast<u32>(AssetType::Last); type++)
				{
					if (equals_ignore_case(*assetName, asset_type_name(static_cast<AssetType>(type))))
					{
						field.Meta = FieldMeta::asset(static_cast<AssetType>(type));
					}
				}
				if (field.Meta.Asset == AssetType::None)
				{
					error = "property '" + name + "' names an unknown asset type '" + *assetName + "'";
					return {};
				}
			}

			if (const sol::optional<std::string> tooltip = entry["tooltip"])
			{
				field.Meta.Tooltip = *tooltip;
			}

			const sol::object defaultValue = entry["default"];
			if (defaultValue.valid() && defaultValue.get_type() != sol::type::lua_nil)
			{
				if (!read_default(defaultValue, field))
				{
					error = "property '" + name + "': the default is not a valid " + field_type_name(field.Type);
					return {};
				}
			}

			fields.push_back(std::move(field));
		}

		std::sort(fields.begin(), fields.end(), [](const FieldDescriptor& a, const FieldDescriptor& b) { return a.Name < b.Name; });
		return fields;
	}
}
