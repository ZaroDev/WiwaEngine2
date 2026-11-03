#include <pch.h>
#include "input_overrides.h"

#include "input_map_io.h"

namespace wiwa
{
	using Json = nlohmann::json;

	Json overrides_to_json(const ActionSystem& actions)
	{
		Json maps = Json::object();
		for (const ActionMap& map : actions.maps())
		{
			Json changed = Json::object();
			for (const Action& action : map.Actions)
			{
				if (!actions.modified(map.Name, action.Name)) continue;

				Json bindings = Json::array();
				for (const Binding& binding : action.Bindings)
				{
					bindings.push_back(binding_to_json(binding));
				}
				changed[action.Name] = std::move(bindings);
			}
			if (!changed.empty())
			{
				maps[map.Name] = std::move(changed);
			}
		}
		return { { "version", INPUT_OVERRIDES_VERSION }, { "maps", maps } };
	}

	size_t apply_overrides(ActionSystem& actions, const Json& overrides, std::string& warnings)
	{
		auto warn = [&warnings](const std::string& text) { warnings += text + "\n"; };

		if (!overrides.is_object())
		{
			warn("the overrides are not a JSON object");
			return 0;
		}
		const auto version = overrides.find("version");
		if (version == overrides.end() || !version->is_number_unsigned() || version->get<u32>() != INPUT_OVERRIDES_VERSION)
		{
			warn("the overrides are of an unsupported version");
			return 0;
		}
		const auto maps = overrides.find("maps");
		if (maps == overrides.end() || !maps->is_object())
		{
			warn("the overrides have no 'maps' object");
			return 0;
		}

		size_t applied = 0;
		for (const auto& [mapName, mapJson] : maps->items())
		{
			if (!actions.find_map(mapName))
			{
				warn("skipped overrides of map '" + mapName + "': there is no such map any more");
				continue;
			}
			if (!mapJson.is_object())
			{
				warn("skipped overrides of map '" + mapName + "': not an object");
				continue;
			}

			for (const auto& [actionName, bindingsJson] : mapJson.items())
			{
				const std::string where = "'" + mapName + "/" + actionName + "'";
				if (!actions.has_action(mapName, actionName))
				{
					warn("skipped overrides of " + where + ": there is no such action any more");
					continue;
				}
				if (!bindingsJson.is_array())
				{
					warn("skipped overrides of " + where + ": not a list of bindings");
					continue;
				}

				std::vector<Binding> bindings;
				bool valid = true;
				for (const Json& bindingJson : bindingsJson)
				{
					std::string error;
					std::optional<Binding> binding = binding_from_json(bindingJson, error);
					if (!binding)
					{
						warn("skipped overrides of " + where + ": " + error);
						valid = false;
						break;
					}
					bindings.push_back(*binding);
				}
				if (!valid) continue;

				if (!actions.set_bindings(mapName, actionName, std::move(bindings)))
				{
					warn("skipped overrides of " + where + ": a binding does not suit the action");
					continue;
				}
				applied++;
			}
		}
		return applied;
	}
}
