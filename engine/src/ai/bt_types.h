#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <core/common.h>

namespace wiwa
{
	// The type of a blackboard key. Written to .wbt files by name: append, never reorder.
	enum class BtValueType : u8 { Bool, Int, Float, Vec3, String, Entity };

	const char* bt_value_type_name(BtValueType type);
	// nullopt for a name that is not a type.
	std::optional<BtValueType> bt_value_type_from_name(const std::string& name);

	// One node of a tree as the file holds it. Children are in tick order. Params is a JSON object whose keys the node type defines (bt_file.h).
	struct BtNodeDesc
	{
		std::string Id{};
		std::string Type{};
		std::string Name{};
		nlohmann::json Params = nlohmann::json::object();
		std::vector<std::string> Children{};

		bool operator==(const BtNodeDesc&) const = default;
	};

	// A blackboard key the tree declares, with its default (JSON: a bool, number, [x,y,z], string, a 32 hex digit entity guid or null).
	struct BtKeyDesc
	{
		std::string Key{};
		BtValueType Type = BtValueType::Bool;
		nlohmann::json Default{};

		bool operator==(const BtKeyDesc&) const = default;
	};

	struct BehaviorTreeDoc
	{
		std::string Root{};
		std::vector<BtNodeDesc> Nodes{};
		std::vector<BtKeyDesc> Blackboard{};

		const BtNodeDesc* find(const std::string& id) const
		{
			for (const BtNodeDesc& node : Nodes)
			{
				if (node.Id == id)
				{
					return &node;
				}
			}
			return nullptr;
		}

		const BtKeyDesc* find_key(const std::string& key) const
		{
			for (const BtKeyDesc& desc : Blackboard)
			{
				if (desc.Key == key)
				{
					return &desc;
				}
			}
			return nullptr;
		}

		bool operator==(const BehaviorTreeDoc&) const = default;
	};
}
