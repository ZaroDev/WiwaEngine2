#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

#include <scene/entity_guid.h>
#include "bt_types.h"

namespace wiwa
{
	// The alternatives are in the order of BtValueType.
	using BtValue = std::variant<bool, i32, float, glm::vec3, std::string, EntityGuid>;

	BtValueType bt_value_type_of(const BtValue& value);
	// The value a JSON literal stands for in a key of that type (null for an entity key is the null guid); nullopt if it does not fit (bt_json_fits).
	std::optional<BtValue> bt_value_from_json(BtValueType type, const nlohmann::json& literal);
	// The zero value of a type (false, 0, 0, the origin, "", the null guid).
	BtValue bt_zero_value(BtValueType type);
	std::string bt_value_to_text(const BtValue& value);

	// The values of one controller, by name. The keys a tree declares are typed (a set of the wrong type is refused); a key the tree does not declare
	// can only be made with set_dynamic (perception does), and is typed by its first value. Entities are held by guid.
	class Blackboard
	{
	public:
		// Forgets everything and makes the declared keys with their defaults.
		void declare(const std::vector<BtKeyDesc>& keys);

		// False, with why in error, for a key that is not declared or a value of another type than the key's (no conversions: a script converts).
		bool set(const std::string& key, const BtValue& value, std::string* error = nullptr);
		// Makes the key if it is not there (an undeclared one); a value of another type than an existing key's is refused.
		bool set_dynamic(const std::string& key, const BtValue& value);

		std::optional<BtValue> get(const std::string& key) const;
		std::optional<BtValueType> type_of(const std::string& key) const;
		bool has(const std::string& key) const { return m_Values.find(key) != m_Values.end(); }
		bool declared(const std::string& key) const { return m_Declared.find(key) != m_Declared.end(); }
		size_t size() const { return m_Values.size(); }

		// What the controller overrides: "key": literal, applied over the defaults. A key that is not declared or a literal that does not fit is skipped,
		// and named in the returned list of problems.
		std::vector<std::string> apply_overrides(const nlohmann::json& overrides);

	private:
		std::unordered_map<std::string, BtValue> m_Values{};
		std::unordered_map<std::string, BtValueType> m_Declared{};
	};
}
