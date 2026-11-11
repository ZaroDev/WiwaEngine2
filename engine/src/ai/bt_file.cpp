#include <pch.h>
#include "bt_file.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

namespace wiwa
{
	using Json = nlohmann::json;

	const char* bt_value_type_name(BtValueType type)
	{
		switch (type)
		{
		case BtValueType::Bool: return "bool";
		case BtValueType::Int: return "int";
		case BtValueType::Float: return "float";
		case BtValueType::Vec3: return "vec3";
		case BtValueType::String: return "string";
		case BtValueType::Entity: return "entity";
		}
		return "bool";
	}

	std::optional<BtValueType> bt_value_type_from_name(const std::string& name)
	{
		for (const BtValueType type : { BtValueType::Bool, BtValueType::Int, BtValueType::Float, BtValueType::Vec3, BtValueType::String, BtValueType::Entity })
		{
			if (name == bt_value_type_name(type))
			{
				return type;
			}
		}
		return std::nullopt;
	}

	namespace
	{
		bool is_hex_guid(const std::string& text)
		{
			return text.size() == 32 && std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
		}

		bool is_vec3(const Json& value)
		{
			return value.is_array() && value.size() == 3 && std::all_of(value.begin(), value.end(), [](const Json& v) { return v.is_number() && std::isfinite(v.get<double>()); });
		}
	}

	bool bt_json_fits(BtValueType type, const Json& value)
	{
		switch (type)
		{
		case BtValueType::Bool: return value.is_boolean();
		case BtValueType::Int: return value.is_number_integer();
		case BtValueType::Float: return value.is_number() && std::isfinite(value.get<double>());
		case BtValueType::Vec3: return is_vec3(value);
		case BtValueType::String: return value.is_string();
		case BtValueType::Entity: return value.is_null() || (value.is_string() && is_hex_guid(value.get<std::string>()));
		}
		return false;
	}

	const std::vector<BtNodeSpec>& bt_builtin_node_specs()
	{
		static const std::vector<BtNodeSpec> specs = []()
		{
			using K = BtParamKind;
			std::vector<BtNodeSpec> s;
			s.push_back({ "Sequence", 1, -1, { { "reactive", K::Bool, false, {} } } });
			s.push_back({ "Selector", 1, -1, { { "reactive", K::Bool, false, {} } } });
			s.push_back({ "Parallel", 2, -1, { { "success", K::Policy, false, {} }, { "failure", K::Policy, false, {} } } });
			s.push_back({ "Inverter", 1, 1, {} });
			s.push_back({ "Succeeder", 1, 1, {} });
			s.push_back({ "Repeat", 1, 1, { { "count", K::Number, false, {} } } });
			s.push_back({ "Cooldown", 1, 1, { { "seconds", K::Number, true, {} } } });
			s.push_back({ "Guard", 1, 1, { { "key", K::Key, true, {} }, { "op", K::Op, true, {} }, { "value", K::Value, false, "key" } } });
			s.push_back({ "Wait", 0, 0, { { "seconds", K::Number, false, {} }, { "min", K::Number, false, {} }, { "max", K::Number, false, {} } } });
			s.push_back({ "SetBlackboard", 0, 0, { { "key", K::Key, true, {} }, { "value", K::Value, true, "key" } } });
			s.push_back({ "CheckBlackboard", 0, 0, { { "key", K::Key, true, {} }, { "op", K::Op, true, {} }, { "value", K::Value, false, "key" } } });
			s.push_back({ "Log", 0, 0, { { "message", K::String, false, {} } } });
			s.push_back({ "MoveTo", 0, 0, { { "target", K::Target, true, {} }, { "stoppingDistance", K::Number, false, {} } } });
			s.push_back({ "PlayAnimation", 0, 0, { { "clip", K::String, true, {} }, { "wait", K::Bool, false, {} } } });
			s.push_back({ "LuaAction", 0, 0, { { "script", K::Script, true, {} } } });
			s.push_back({ "LuaCondition", 0, 0, { { "script", K::Script, true, {} } } });
			return s;
		}();
		return specs;
	}

	const BtNodeSpec* bt_find_node_spec(const std::string& type)
	{
		for (const BtNodeSpec& spec : bt_builtin_node_specs())
		{
			if (spec.Type == type)
			{
				return &spec;
			}
		}
		return nullptr;
	}

	std::optional<BehaviorTreeDoc> read_bt_file(std::string_view text, std::string& error)
	{
		const Json root = Json::parse(text.begin(), text.end(), nullptr, false);
		if (root.is_discarded() || !root.is_object())
		{
			error = "not a JSON object";
			return std::nullopt;
		}
		const auto version = root.find("version");
		if (version == root.end() || !version->is_number_integer() || version->get<i64>() != WBT_VERSION)
		{
			error = "\"version\" must be " + std::to_string(WBT_VERSION);
			return std::nullopt;
		}

		BehaviorTreeDoc doc;
		const auto rootIt = root.find("root");
		if (rootIt == root.end() || !rootIt->is_string())
		{
			error = "\"root\" must be a node id (a string)";
			return std::nullopt;
		}
		doc.Root = rootIt->get<std::string>();

		if (const auto board = root.find("blackboard"); board != root.end())
		{
			if (!board->is_array())
			{
				error = "\"blackboard\" must be a list";
				return std::nullopt;
			}
			for (const Json& entry : *board)
			{
				if (!entry.is_object() || !entry.contains("key") || !entry["key"].is_string() || !entry.contains("type") || !entry["type"].is_string())
				{
					error = "every blackboard entry needs a string \"key\" and \"type\"";
					return std::nullopt;
				}
				const std::optional<BtValueType> type = bt_value_type_from_name(entry["type"].get<std::string>());
				if (!type)
				{
					error = "blackboard key \"" + entry["key"].get<std::string>() + "\" has an unknown type (bool, int, float, vec3, string or entity)";
					return std::nullopt;
				}
				BtKeyDesc key;
				key.Key = entry["key"].get<std::string>();
				key.Type = *type;
				key.Default = entry.contains("default") ? entry["default"] : Json();
				doc.Blackboard.push_back(std::move(key));
			}
		}

		const auto nodes = root.find("nodes");
		if (nodes == root.end() || !nodes->is_array())
		{
			error = "\"nodes\" must be a list";
			return std::nullopt;
		}
		for (const Json& entry : *nodes)
		{
			if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_string() || !entry.contains("type") || !entry["type"].is_string())
			{
				error = "every node needs a string \"id\" and \"type\"";
				return std::nullopt;
			}
			BtNodeDesc node;
			node.Id = entry["id"].get<std::string>();
			node.Type = entry["type"].get<std::string>();
			if (entry.contains("name"))
			{
				if (!entry["name"].is_string())
				{
					error = "node " + node.Id + ": \"name\" must be a string";
					return std::nullopt;
				}
				node.Name = entry["name"].get<std::string>();
			}
			if (entry.contains("params"))
			{
				if (!entry["params"].is_object())
				{
					error = "node " + node.Id + ": \"params\" must be an object";
					return std::nullopt;
				}
				node.Params = entry["params"];
			}
			if (entry.contains("children"))
			{
				if (!entry["children"].is_array())
				{
					error = "node " + node.Id + ": \"children\" must be a list of node ids";
					return std::nullopt;
				}
				for (const Json& child : entry["children"])
				{
					if (!child.is_string())
					{
						error = "node " + node.Id + ": \"children\" must be a list of node ids";
						return std::nullopt;
					}
					node.Children.push_back(child.get<std::string>());
				}
			}
			doc.Nodes.push_back(std::move(node));
		}
		return doc;
	}

	std::string write_bt_file(const BehaviorTreeDoc& doc)
	{
		Json root = Json::object();
		root["version"] = WBT_VERSION;
		root["root"] = doc.Root;
		Json board = Json::array();
		for (const BtKeyDesc& key : doc.Blackboard)
		{
			board.push_back({ { "key", key.Key }, { "type", bt_value_type_name(key.Type) }, { "default", key.Default } });
		}
		root["blackboard"] = board;
		Json nodes = Json::array();
		for (const BtNodeDesc& node : doc.Nodes)
		{
			Json entry = Json::object();
			entry["id"] = node.Id;
			entry["type"] = node.Type;
			entry["name"] = node.Name;
			entry["params"] = node.Params.is_object() ? node.Params : Json::object();
			entry["children"] = node.Children;
			nodes.push_back(std::move(entry));
		}
		root["nodes"] = nodes;
		return root.dump(2) + "\n";
	}

	bool bt_has_errors(const std::vector<BtProblem>& problems)
	{
		return std::any_of(problems.begin(), problems.end(), [](const BtProblem& p) { return p.Error; });
	}

	namespace
	{
		bool op_known(const std::string& op)
		{
			return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=" || op == "true" || op == "false";
		}

		// Whether a comparison makes sense for a key type.
		bool op_fits(BtValueType type, const std::string& op)
		{
			switch (type)
			{
			case BtValueType::Bool: return op == "==" || op == "!=" || op == "true" || op == "false";
			case BtValueType::Int:
			case BtValueType::Float: return op != "true" && op != "false";
			default: return op == "==" || op == "!=";
			}
		}

		void check_params(const BehaviorTreeDoc& doc, const BtNodeDesc& node, const BtNodeSpec& spec, std::vector<BtProblem>& problems)
		{
			const bool lua = node.Type == "LuaAction" || node.Type == "LuaCondition";
			const auto problem = [&](const std::string& message, bool error = true) { problems.push_back({ node.Id, message, error }); };

			for (const BtParamSpec& param : spec.Params)
			{
				const auto it = node.Params.find(param.Name);
				const bool present = it != node.Params.end();
				if (!present)
				{
					if (param.Kind == BtParamKind::Value)
					{
						const auto named = node.Params.find(param.KeyParam);
						const BtKeyDesc* named_key = named != node.Params.end() && named->is_string() ? doc.find_key(named->get<std::string>()) : nullptr;
						if (named_key != nullptr && named_key->Type == BtValueType::Entity)
						{
							continue; // an entity key's value is none when it is not given
						}
					}
					if (param.Required)
					{
						problem("missing parameter \"" + param.Name + "\"");
					}
					continue;
				}
				const Json& value = *it;
				switch (param.Kind)
				{
				case BtParamKind::Bool:
					if (!value.is_boolean()) { problem("\"" + param.Name + "\" must be true or false"); }
					break;
				case BtParamKind::Number:
					if (!value.is_number() || !std::isfinite(value.get<double>()) || value.get<double>() < 0.0) { problem("\"" + param.Name + "\" must be a number of at least 0"); }
					break;
				case BtParamKind::String:
					if (!value.is_string()) { problem("\"" + param.Name + "\" must be text"); }
					break;
				case BtParamKind::Op:
					if (!value.is_string() || !op_known(value.get<std::string>())) { problem("\"" + param.Name + "\" must be one of ==, !=, <, <=, >, >=, true, false"); }
					break;
				case BtParamKind::Policy:
					if (!value.is_string() || (value.get<std::string>() != "all" && value.get<std::string>() != "one")) { problem("\"" + param.Name + "\" must be \"all\" or \"one\""); }
					break;
				case BtParamKind::Key:
					if (!value.is_string() || doc.find_key(value.get<std::string>()) == nullptr) { problem("\"" + param.Name + "\" must name a declared blackboard key"); }
					break;
				case BtParamKind::Script:
					if (!value.is_string() || !is_hex_guid(value.get<std::string>())) { problem("\"" + param.Name + "\" must be the guid of a script asset"); }
					break;
				case BtParamKind::Target:
				{
					bool ok = false;
					if (is_vec3(value))
					{
						ok = true;
					}
					else if (value.is_string() && value.get<std::string>().rfind("key:", 0) == 0)
					{
						const BtKeyDesc* key = doc.find_key(value.get<std::string>().substr(4));
						ok = key != nullptr && (key->Type == BtValueType::Vec3 || key->Type == BtValueType::Entity);
					}
					if (!ok) { problem("\"" + param.Name + "\" must be [x, y, z] or \"key:<name>\" of a vec3 or entity key"); }
					break;
				}
				case BtParamKind::Value:
					break; // below, with its key
				}
			}

			// A comparison and its literal fit the key's type.
			const auto keyParam = node.Params.find("key");
			const BtKeyDesc* key = (keyParam != node.Params.end() && keyParam->is_string()) ? doc.find_key(keyParam->get<std::string>()) : nullptr;
			if (key != nullptr)
			{
				const auto opParam = node.Params.find("op");
				std::string op;
				if (opParam != node.Params.end() && opParam->is_string() && op_known(opParam->get<std::string>()))
				{
					op = opParam->get<std::string>();
					if (!op_fits(key->Type, op))
					{
						problem("the comparison \"" + op + "\" does not fit the " + bt_value_type_name(key->Type) + " key \"" + key->Key + "\"");
					}
				}
				const auto valueParam = node.Params.find("value");
				if (valueParam != node.Params.end() && !bt_json_fits(key->Type, *valueParam))
				{
					problem("\"value\" does not fit the " + std::string(bt_value_type_name(key->Type)) + " key \"" + key->Key + "\"");
				}
				if (valueParam == node.Params.end() && !op.empty() && op != "true" && op != "false" && node.Type != "SetBlackboard" && key->Type != BtValueType::Entity)
				{
					problem("a comparison needs a \"value\"");
				}
			}

			if (!lua)
			{
				for (const auto& [name, value] : node.Params.items())
				{
					const bool known = std::any_of(spec.Params.begin(), spec.Params.end(), [&](const BtParamSpec& p) { return p.Name == name; });
					if (!known && name != "ui")
					{
						problem("unknown parameter \"" + name + "\"", false);
					}
				}
			}
		}
	}

	std::vector<BtProblem> validate_bt(const BehaviorTreeDoc& doc)
	{
		std::vector<BtProblem> problems;

		std::set<std::string> keys;
		for (const BtKeyDesc& key : doc.Blackboard)
		{
			if (key.Key.empty())
			{
				problems.push_back({ {}, "a blackboard key has no name", true });
			}
			else if (!keys.insert(key.Key).second)
			{
				problems.push_back({ {}, "blackboard key \"" + key.Key + "\" is declared twice", true });
			}
			if (!key.Default.is_null() || key.Type == BtValueType::Entity)
			{
				if (!bt_json_fits(key.Type, key.Default))
				{
					problems.push_back({ {}, "the default of blackboard key \"" + key.Key + "\" does not fit its type " + bt_value_type_name(key.Type), true });
				}
			}
		}

		std::set<std::string> ids;
		for (const BtNodeDesc& node : doc.Nodes)
		{
			if (node.Id.empty() || !ids.insert(node.Id).second)
			{
				problems.push_back({ node.Id, node.Id.empty() ? "a node has no id" : "the id is used by another node", true });
			}
		}

		if (doc.find(doc.Root) == nullptr)
		{
			problems.push_back({ {}, doc.Root.empty() ? "the tree has no root" : "the root \"" + doc.Root + "\" is not a node", true });
			return problems;
		}

		// Walk from the root: a node reached twice is shared or in a cycle.
		std::set<std::string> reached;
		std::vector<std::string> stack{ doc.Root };
		reached.insert(doc.Root);
		while (!stack.empty())
		{
			const std::string id = stack.back();
			stack.pop_back();
			const BtNodeDesc* node = doc.find(id);
			if (node == nullptr)
			{
				continue;
			}
			for (const std::string& child : node->Children)
			{
				if (doc.find(child) == nullptr)
				{
					problems.push_back({ id, "the child \"" + child + "\" is not a node", true });
					continue;
				}
				if (!reached.insert(child).second)
				{
					problems.push_back({ child, "the node is the child of more than one node, or part of a cycle", true });
					continue;
				}
				stack.push_back(child);
			}
		}

		for (const BtNodeDesc& node : doc.Nodes)
		{
			const BtNodeSpec* spec = bt_find_node_spec(node.Type);
			if (spec == nullptr)
			{
				problems.push_back({ node.Id, "unknown node type \"" + node.Type + "\"", true });
				continue;
			}
			const int count = static_cast<int>(node.Children.size());
			if (count < spec->MinChildren || (spec->MaxChildren >= 0 && count > spec->MaxChildren))
			{
				std::string need = spec->Leaf() ? "no children" : (spec->MaxChildren < 0 ? "at least " + std::to_string(spec->MinChildren) + " children" : "exactly " + std::to_string(spec->MinChildren) + " child");
				problems.push_back({ node.Id, node.Type + " needs " + need + " (has " + std::to_string(count) + ")", true });
			}
			check_params(doc, node, *spec, problems);
			if (reached.find(node.Id) == reached.end())
			{
				problems.push_back({ node.Id, "the root does not reach this node", false });
			}
		}
		return problems;
	}
}
