#include <pch.h>
#include "blackboard.h"

#include "bt_file.h"

namespace wiwa
{
	BtValueType bt_value_type_of(const BtValue& value)
	{
		return static_cast<BtValueType>(value.index());
	}

	BtValue bt_zero_value(BtValueType type)
	{
		switch (type)
		{
		case BtValueType::Bool: return false;
		case BtValueType::Int: return i32{ 0 };
		case BtValueType::Float: return 0.0f;
		case BtValueType::Vec3: return glm::vec3(0.0f);
		case BtValueType::String: return std::string();
		case BtValueType::Entity: return EntityGuid{};
		}
		return false;
	}

	std::optional<BtValue> bt_value_from_json(BtValueType type, const nlohmann::json& literal)
	{
		if (literal.is_null() && type != BtValueType::Entity)
		{
			return bt_zero_value(type); // a key declared without a default
		}
		if (!bt_json_fits(type, literal))
		{
			return std::nullopt;
		}
		switch (type)
		{
		case BtValueType::Bool: return literal.get<bool>();
		case BtValueType::Int: return static_cast<i32>(literal.get<i64>());
		case BtValueType::Float: return literal.get<float>();
		case BtValueType::Vec3: return glm::vec3(literal[0].get<float>(), literal[1].get<float>(), literal[2].get<float>());
		case BtValueType::String: return literal.get<std::string>();
		case BtValueType::Entity:
		{
			if (literal.is_null())
			{
				return EntityGuid{};
			}
			const std::optional<EntityGuid> guid = EntityGuid::from_string(literal.get<std::string>());
			if (!guid)
			{
				return std::nullopt;
			}
			return *guid;
		}
		}
		return std::nullopt;
	}

	std::string bt_value_to_text(const BtValue& value)
	{
		switch (bt_value_type_of(value))
		{
		case BtValueType::Bool: return std::get<bool>(value) ? "true" : "false";
		case BtValueType::Int: return std::to_string(std::get<i32>(value));
		case BtValueType::Float: return std::to_string(std::get<float>(value));
		case BtValueType::Vec3:
		{
			const glm::vec3& v = std::get<glm::vec3>(value);
			return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
		}
		case BtValueType::String: return std::get<std::string>(value);
		case BtValueType::Entity: return std::get<EntityGuid>(value).valid() ? std::get<EntityGuid>(value).to_string() : "none";
		}
		return {};
	}

	void Blackboard::declare(const std::vector<BtKeyDesc>& keys)
	{
		m_Values.clear();
		m_Declared.clear();
		for (const BtKeyDesc& key : keys)
		{
			m_Declared[key.Key] = key.Type;
			m_Values[key.Key] = bt_value_from_json(key.Type, key.Default).value_or(bt_zero_value(key.Type));
		}
	}

	bool Blackboard::set(const std::string& key, const BtValue& value, std::string* error)
	{
		const auto declared = m_Declared.find(key);
		if (declared == m_Declared.end())
		{
			if (error != nullptr)
			{
				*error = "the blackboard has no key \"" + key + "\"";
			}
			return false;
		}
		if (bt_value_type_of(value) != declared->second)
		{
			if (error != nullptr)
			{
				*error = "the key \"" + key + "\" is " + bt_value_type_name(declared->second) + ", not " + bt_value_type_name(bt_value_type_of(value));
			}
			return false;
		}
		if (const float* number = std::get_if<float>(&value); number != nullptr && !std::isfinite(*number))
		{
			if (error != nullptr)
			{
				*error = "the key \"" + key + "\" cannot hold a value that is not a number";
			}
			return false;
		}
		if (const glm::vec3* point = std::get_if<glm::vec3>(&value); point != nullptr && !(std::isfinite(point->x) && std::isfinite(point->y) && std::isfinite(point->z)))
		{
			if (error != nullptr)
			{
				*error = "the key \"" + key + "\" cannot hold a point that is not a number";
			}
			return false;
		}
		m_Values[key] = value;
		return true;
	}

	bool Blackboard::set_dynamic(const std::string& key, const BtValue& value)
	{
		const auto existing = m_Values.find(key);
		if (existing != m_Values.end() && bt_value_type_of(existing->second) != bt_value_type_of(value))
		{
			return false;
		}
		m_Values[key] = value;
		return true;
	}

	std::optional<BtValue> Blackboard::get(const std::string& key) const
	{
		const auto it = m_Values.find(key);
		if (it == m_Values.end())
		{
			return std::nullopt;
		}
		return it->second;
	}

	std::optional<BtValueType> Blackboard::type_of(const std::string& key) const
	{
		const auto it = m_Values.find(key);
		if (it == m_Values.end())
		{
			return std::nullopt;
		}
		return bt_value_type_of(it->second);
	}

	std::vector<std::string> Blackboard::apply_overrides(const nlohmann::json& overrides)
	{
		std::vector<std::string> problems;
		if (!overrides.is_object())
		{
			return problems;
		}
		for (const auto& [key, literal] : overrides.items())
		{
			const auto declared = m_Declared.find(key);
			if (declared == m_Declared.end())
			{
				problems.push_back("override of \"" + key + "\": the tree has no such key");
				continue;
			}
			const std::optional<BtValue> value = literal.is_null() ? std::optional<BtValue>(bt_zero_value(declared->second)) : bt_value_from_json(declared->second, literal);
			if (!value)
			{
				problems.push_back("override of \"" + key + "\": the value does not fit " + bt_value_type_name(declared->second));
				continue;
			}
			m_Values[key] = *value;
		}
		return problems;
	}
}
