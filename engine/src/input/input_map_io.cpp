#include <pch.h>
#include "input_map_io.h"

#include <cmath>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace wiwa
{
	using Json = nlohmann::json;

	size_t binding_part_count(Binding::Kind kind)
	{
		switch (kind)
		{
		case Binding::Kind::Control: return 1;
		case Binding::Kind::Composite1D: return 2;
		case Binding::Kind::Composite2D: return 4;
		case Binding::Kind::Pair2D: return 2;
		}
		return 0;
	}

	const char* binding_part_name(Binding::Kind kind, size_t part)
	{
		static const char* const control[] = { "control" };
		static const char* const composite1d[] = { "negative", "positive" };
		static const char* const composite2d[] = { "up", "down", "left", "right" };
		static const char* const pair2d[] = { "x", "y" };

		if (part >= binding_part_count(kind)) return "";
		switch (kind)
		{
		case Binding::Kind::Control: return control[part];
		case Binding::Kind::Composite1D: return composite1d[part];
		case Binding::Kind::Composite2D: return composite2d[part];
		case Binding::Kind::Pair2D: return pair2d[part];
		}
		return "";
	}

	const char* action_type_name(ActionType type)
	{
		switch (type)
		{
		case ActionType::Button: return "Button";
		case ActionType::Axis1D: return "Axis1D";
		case ActionType::Axis2D: return "Axis2D";
		}
		return "";
	}

	bool parse_action_type(std::string_view name, ActionType& out)
	{
		for (ActionType type : { ActionType::Button, ActionType::Axis1D, ActionType::Axis2D })
		{
			if (name == action_type_name(type))
			{
				out = type;
				return true;
			}
		}
		return false;
	}

	bool action_accepts(ActionType type, Binding::Kind kind)
	{
		switch (type)
		{
		case ActionType::Button: return kind == Binding::Kind::Control;
		case ActionType::Axis1D: return kind == Binding::Kind::Control || kind == Binding::Kind::Composite1D;
		case ActionType::Axis2D: return kind == Binding::Kind::Composite2D || kind == Binding::Kind::Pair2D;
		}
		return false;
	}

	bool input_map_valid(const InputMapData& data, std::string& error)
	{
		std::unordered_set<std::string> mapNames;
		for (const ActionMap& map : data.Maps)
		{
			if (map.Name.empty())
			{
				error = "a map has an empty name";
				return false;
			}
			if (!mapNames.insert(map.Name).second)
			{
				error = "duplicate map name '" + map.Name + "'";
				return false;
			}

			std::unordered_set<std::string> actionNames;
			for (const Action& action : map.Actions)
			{
				if (action.Name.empty())
				{
					error = "map '" + map.Name + "' has an action with an empty name";
					return false;
				}
				if (!actionNames.insert(action.Name).second)
				{
					error = "map '" + map.Name + "' has a duplicate action '" + action.Name + "'";
					return false;
				}

				for (const Binding& binding : action.Bindings)
				{
					if (!action_accepts(action.Type, binding.BindingKind))
					{
						error = "action '" + action.Name + "' (" + action_type_name(action.Type) + ") in map '" + map.Name + "' has a binding it cannot use";
						return false;
					}
					if (!std::isfinite(binding.Scale))
					{
						error = "action '" + action.Name + "' has a scale that is not a number";
						return false;
					}
					if (!(binding.Deadzone >= 0.0f && binding.Deadzone < 1.0f))
					{
						error = "action '" + action.Name + "' has a deadzone outside [0, 1)";
						return false;
					}
					for (size_t part = 0; part < binding_part_count(binding.BindingKind); part++)
					{
						if (control_name(binding.Parts[part]).empty())
						{
							error = "action '" + action.Name + "' has a binding with an invalid control";
							return false;
						}
					}
				}
			}
		}
		return true;
	}

	Json binding_to_json(const Binding& binding)
	{
		Json json = Json::object();
		for (size_t part = 0; part < binding_part_count(binding.BindingKind); part++)
		{
			json[binding_part_name(binding.BindingKind, part)] = control_name(binding.Parts[part]);
		}
		if (binding.Scale != 1.0f) json["scale"] = binding.Scale;
		if (binding.Deadzone != 0.0f) json["deadzone"] = binding.Deadzone;
		if (binding.Invert) json["invert"] = true;
		return json;
	}

	namespace
	{
		bool read_number(const Json& json, const char* key, float& out, std::string& error)
		{
			auto it = json.find(key);
			if (it == json.end()) return true;
			if (!it->is_number())
			{
				error = std::string("'") + key + "' is not a number";
				return false;
			}
			out = it->get<float>();
			return true;
		}
	}

	std::optional<Binding> binding_from_json(const Json& json, std::string& error)
	{
		if (!json.is_object())
		{
			error = "a binding is not an object";
			return std::nullopt;
		}

		Binding binding;
		bool found = false;
		for (Binding::Kind kind : { Binding::Kind::Control, Binding::Kind::Composite1D, Binding::Kind::Composite2D, Binding::Kind::Pair2D })
		{
			bool all = true;
			for (size_t part = 0; part < binding_part_count(kind); part++)
			{
				all = all && json.contains(binding_part_name(kind, part));
			}
			if (!all) continue;

			if (found)
			{
				error = "a binding mixes the keys of two binding kinds";
				return std::nullopt;
			}
			found = true;
			binding.BindingKind = kind;
		}
		if (!found)
		{
			error = "a binding has none of the key sets control / negative+positive / up+down+left+right / x+y";
			return std::nullopt;
		}

		for (size_t part = 0; part < binding_part_count(binding.BindingKind); part++)
		{
			const Json& value = json[binding_part_name(binding.BindingKind, part)];
			std::optional<InputControl> parsed;
			if (value.is_string())
			{
				parsed = parse_control(value.get<std::string>());
			}
			if (!parsed)
			{
				error = "unknown control " + (value.is_string() ? "'" + value.get<std::string>() + "'" : value.dump());
				return std::nullopt;
			}
			binding.Parts[part] = *parsed;
		}

		if (!read_number(json, "scale", binding.Scale, error) || !read_number(json, "deadzone", binding.Deadzone, error))
		{
			return std::nullopt;
		}
		if (auto it = json.find("invert"); it != json.end())
		{
			if (!it->is_boolean())
			{
				error = "'invert' is not a boolean";
				return std::nullopt;
			}
			binding.Invert = it->get<bool>();
		}
		return binding;
	}

	std::string input_map_to_text(const InputMapData& data)
	{
		Json maps = Json::array();
		for (const ActionMap& map : data.Maps)
		{
			Json actions = Json::array();
			for (const Action& action : map.Actions)
			{
				Json bindings = Json::array();
				for (const Binding& binding : action.Bindings)
				{
					bindings.push_back(binding_to_json(binding));
				}
				actions.push_back({ { "name", action.Name }, { "type", action_type_name(action.Type) }, { "bindings", bindings } });
			}
			maps.push_back({ { "name", map.Name }, { "consume", map.Consume }, { "actions", actions } });
		}

		Json root = { { "version", INPUT_MAP_VERSION }, { "maps", maps } };
		return root.dump(1, '\t') + "\n";
	}

	bool input_map_from_text(const std::string& text, InputMapData& out, std::string& error)
	{
		Json root = Json::parse(text, nullptr, false);
		if (root.is_discarded() || !root.is_object())
		{
			error = "the file is not a JSON object";
			return false;
		}

		auto version = root.find("version");
		if (version == root.end() || !version->is_number_unsigned() || version->get<u32>() != INPUT_MAP_VERSION)
		{
			error = "unsupported version (expected version " + std::to_string(INPUT_MAP_VERSION) + ")";
			return false;
		}

		auto mapsJson = root.find("maps");
		if (mapsJson == root.end() || !mapsJson->is_array())
		{
			error = "'maps' is missing or not an array";
			return false;
		}

		InputMapData parsed;
		for (const Json& mapJson : *mapsJson)
		{
			if (!mapJson.is_object() || !mapJson.contains("name") || !mapJson["name"].is_string())
			{
				error = "a map has no name";
				return false;
			}

			ActionMap map;
			map.Name = mapJson["name"].get<std::string>();
			if (auto consume = mapJson.find("consume"); consume != mapJson.end())
			{
				if (!consume->is_boolean())
				{
					error = "map '" + map.Name + "': 'consume' is not a boolean";
					return false;
				}
				map.Consume = consume->get<bool>();
			}

			auto actionsJson = mapJson.find("actions");
			if (actionsJson == mapJson.end() || !actionsJson->is_array())
			{
				error = "map '" + map.Name + "': 'actions' is missing or not an array";
				return false;
			}

			for (const Json& actionJson : *actionsJson)
			{
				if (!actionJson.is_object() || !actionJson.contains("name") || !actionJson["name"].is_string())
				{
					error = "map '" + map.Name + "': an action has no name";
					return false;
				}

				Action action;
				action.Name = actionJson["name"].get<std::string>();

				if (!actionJson.contains("type") || !actionJson["type"].is_string() || !parse_action_type(actionJson["type"].get<std::string>(), action.Type))
				{
					error = "action '" + action.Name + "': unknown type " + (actionJson.contains("type") ? actionJson["type"].dump() : std::string("(missing)"));
					return false;
				}

				auto bindingsJson = actionJson.find("bindings");
				if (bindingsJson == actionJson.end() || !bindingsJson->is_array())
				{
					error = "action '" + action.Name + "': 'bindings' is missing or not an array";
					return false;
				}

				for (const Json& bindingJson : *bindingsJson)
				{
					std::string bindingError;
					std::optional<Binding> binding = binding_from_json(bindingJson, bindingError);
					if (!binding)
					{
						error = "action '" + action.Name + "': " + bindingError;
						return false;
					}
					action.Bindings.push_back(*binding);
				}
				map.Actions.push_back(std::move(action));
			}
			parsed.Maps.push_back(std::move(map));
		}

		if (!input_map_valid(parsed, error))
		{
			return false;
		}

		out = std::move(parsed);
		return true;
	}
}
