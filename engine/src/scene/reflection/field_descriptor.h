#pragma once

#include <string>
#include <vector>

#include <assets/asset_types.h>
#include "field_value.h"

namespace wiwa
{
	// What the inspector needs to know beyond the type.
	struct FieldMeta
	{
		bool HasRange = false;
		float Min = 0.0f;
		float Max = 0.0f;
		bool IsColor = false;                 // a Vec4 that is a color
		AssetType Asset = AssetType::None;    // for an Asset field: the type it accepts
		std::vector<std::string> EnumNames{}; // for an Enum field: the name of each value, in order
		std::string Tooltip{};

		static FieldMeta range(float minimum, float maximum)
		{
			FieldMeta meta;
			meta.HasRange = true;
			meta.Min = minimum;
			meta.Max = maximum;
			return meta;
		}

		static FieldMeta color()
		{
			FieldMeta meta;
			meta.IsColor = true;
			return meta;
		}

		static FieldMeta asset(AssetType type)
		{
			FieldMeta meta;
			meta.Asset = type;
			return meta;
		}

		static FieldMeta enumeration(std::vector<std::string> names)
		{
			FieldMeta meta;
			meta.EnumNames = std::move(names);
			return meta;
		}
	};

	struct FieldDescriptor
	{
		std::string Name{};
		FieldType Type = FieldType::Float;
		FieldMeta Meta{};
		FieldValue Default{};
	};

	// Whether the value can be stored in the field: the right alternative, and an enum inside its names.
	inline bool is_valid_field_value(const FieldDescriptor& field, const FieldValue& value)
	{
		if (!value_matches_type(field.Type, value))
		{
			return false;
		}

		if (field.Type == FieldType::Enum && !field.Meta.EnumNames.empty())
		{
			const i32 index = std::get<i32>(value);
			return index >= 0 && static_cast<size_t>(index) < field.Meta.EnumNames.size();
		}
		return true;
	}
}
