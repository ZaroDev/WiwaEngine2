#pragma once

#include <string>
#include <variant>
#include <vector>

#include <assets/asset_guid.h>
#include <math/math.h>
#include <scene/entity_guid.h>

namespace wiwa
{
	// The kinds of field the serializer, the inspector and the overrides understand.
	enum class FieldType : u32
	{
		Bool,
		Int,
		Float,
		Vec2,
		Vec3,
		Vec4,
		Color,     // a vec4 shown as a color
		Quat,
		String,
		Enum,      // stored as an i32, named by FieldMeta::EnumNames
		Asset,
		AssetList,
		Entity,
	};

	// A field's value. The order of the alternatives is written to scene files as a tag: only ever append.
	using FieldValue = std::variant<bool, i32, float, glm::vec2, glm::vec3, glm::vec4, glm::quat, std::string, AssetRef, std::vector<AssetRef>, EntityGuid>;

	inline bool value_matches_type(FieldType type, const FieldValue& value)
	{
		switch (type)
		{
		case FieldType::Bool: return std::holds_alternative<bool>(value);
		case FieldType::Int:
		case FieldType::Enum: return std::holds_alternative<i32>(value);
		case FieldType::Float: return std::holds_alternative<float>(value);
		case FieldType::Vec2: return std::holds_alternative<glm::vec2>(value);
		case FieldType::Vec3: return std::holds_alternative<glm::vec3>(value);
		case FieldType::Vec4:
		case FieldType::Color: return std::holds_alternative<glm::vec4>(value);
		case FieldType::Quat: return std::holds_alternative<glm::quat>(value);
		case FieldType::String: return std::holds_alternative<std::string>(value);
		case FieldType::Asset: return std::holds_alternative<AssetRef>(value);
		case FieldType::AssetList: return std::holds_alternative<std::vector<AssetRef>>(value);
		case FieldType::Entity: return std::holds_alternative<EntityGuid>(value);
		}
		return false;
	}

	inline const char* field_type_name(FieldType type)
	{
		switch (type)
		{
		case FieldType::Bool: return "bool";
		case FieldType::Int: return "int";
		case FieldType::Float: return "float";
		case FieldType::Vec2: return "vec2";
		case FieldType::Vec3: return "vec3";
		case FieldType::Vec4: return "vec4";
		case FieldType::Color: return "color";
		case FieldType::Quat: return "quat";
		case FieldType::String: return "string";
		case FieldType::Enum: return "enum";
		case FieldType::Asset: return "asset";
		case FieldType::AssetList: return "asset list";
		case FieldType::Entity: return "entity";
		}
		return "?";
	}
}
