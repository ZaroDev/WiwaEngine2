#include <editor_common.h>
#include "nav_settings_file.h"
#include <core/file_io.h>

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace wiwa
{
	const char* nav_area_name(NavArea area)
	{
		switch (area)
		{
		case NavArea::NotWalkable: return "NotWalkable";
		case NavArea::Walkable: return "Walkable";
		case NavArea::Mud: return "Mud";
		case NavArea::Water: return "Water";
		case NavArea::Custom0: return "Custom0";
		case NavArea::Custom1: return "Custom1";
		case NavArea::Custom2: return "Custom2";
		case NavArea::Custom3: return "Custom3";
		case NavArea::Area8: return "Area8";
		case NavArea::Area9: return "Area9";
		case NavArea::Area10: return "Area10";
		case NavArea::Area11: return "Area11";
		case NavArea::Area12: return "Area12";
		case NavArea::Area13: return "Area13";
		case NavArea::Area14: return "Area14";
		case NavArea::Area15: return "Area15";
		}
		return "Walkable";
	}

	bool nav_settings_from_text(const std::string& text, NavSettings& out, std::string& error)
	{
		using Json = nlohmann::json;
		const Json root = Json::parse(text, nullptr, false);
		if (root.is_discarded() || !root.is_object())
		{
			error = "not a JSON object";
			return false;
		}

		NavSettings settings;
		if (const auto areas = root.find("areas"); areas != root.end())
		{
			if (!areas->is_array())
			{
				error = "\"areas\" must be a list of { id, name, cost }";
				return false;
			}
			for (u32 i = 2; i < NAV_AREA_COUNT; i++)
			{
				settings.AreaName[i].clear(); // the file says which slots exist
			}
			std::array<bool, NAV_AREA_COUNT> listed{};
			for (const Json& entry : *areas)
			{
				const auto idIt = entry.is_object() ? entry.find("id") : entry.end();
				if (idIt == entry.end() || !idIt->is_number_integer() || idIt->get<i64>() < 0 || idIt->get<i64>() >= static_cast<i64>(NAV_AREA_COUNT))
				{
					error = "every area needs an \"id\" from 0 to " + std::to_string(NAV_AREA_COUNT - 1);
					return false;
				}
				const u32 id = static_cast<u32>(idIt->get<i64>());
				if (listed[id])
				{
					error = "the area id " + std::to_string(id) + " is listed twice";
					return false;
				}
				listed[id] = true;
				if (const auto cost = entry.find("cost"); cost != entry.end())
				{
					if (!cost->is_number())
					{
						error = "the cost of area " + std::to_string(id) + " must be a number";
						return false;
					}
					settings.AreaCost[id] = cost->get<float>();
				}
				if (id >= 2)
				{
					const auto nameIt = entry.find("name");
					if (nameIt == entry.end() || !nameIt->is_string() || !nav_area_name_ok(settings, id, nameIt->get<std::string>()))
					{
						error = "area " + std::to_string(id) + " needs a name that is not empty, at most " + std::to_string(NAV_AREA_NAME_MAX) + " characters, with no control character, that no other area has";
						return false;
					}
					settings.AreaName[id] = nameIt->get<std::string>();
				}
			}
		}
		else if (const auto costs = root.find("areaCost"); costs != root.end())
		{
			if (!costs->is_object())
			{
				error = "\"areaCost\" must be an object of area names and costs";
				return false;
			}
			for (u32 i = 4; i < 8; i++)
			{
				settings.AreaName[i].clear();
			}
			for (const auto& [name, value] : costs->items())
			{
				int found = -1;
				for (u32 i = 0; i < 8; i++)
				{
					if (name == nav_area_name(static_cast<NavArea>(i)))
					{
						found = static_cast<int>(i);
					}
				}
				if (found < 0)
				{
					error = "\"areaCost\" names an unknown area: " + name;
					return false;
				}
				if (!value.is_number())
				{
					error = "the cost of " + name + " must be a number";
					return false;
				}
				settings.AreaCost[static_cast<size_t>(found)] = value.get<float>();
				if (found >= 4 && value.get<float>() > 1.0f)
				{
					settings.AreaName[static_cast<size_t>(found)] = name; // a legacy Custom area exists when the file gave it a cost above 1
				}
			}
		}

		out = sanitized(settings);
		return true;
	}

	std::string nav_settings_to_text(const NavSettings& settings)
	{
		nlohmann::json root;
		nlohmann::json areas = nlohmann::json::array();
		for (u32 i = 1; i < NAV_AREA_COUNT; i++)
		{
			if (!settings.used(i))
			{
				continue;
			}
			nlohmann::json entry;
			entry["id"] = i;
			if (i >= 2)
			{
				entry["name"] = settings.AreaName[i];
			}
			entry["cost"] = settings.AreaCost[i];
			areas.push_back(entry);
		}
		root["areas"] = areas;
		return root.dump(2);
	}

	bool load_nav_settings(const std::filesystem::path& path, NavSettings& out, std::string& error)
	{
		out = NavSettings{};
		std::error_code exists;
		if (!std::filesystem::exists(path, exists))
		{
			return true;
		}

		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			error = "cannot read " + path.string();
			return false;
		}
		std::stringstream text;
		text << file.rdbuf();
		if (!nav_settings_from_text(text.str(), out, error))
		{
			out = NavSettings{};
			return false;
		}
		return true;
	}

	bool save_nav_settings(const std::filesystem::path& path, const NavSettings& settings, std::string& error)
	{
		const std::string text = nav_settings_to_text(settings);
		return write_file_atomic(path, text.data(), text.size(), error);
	}
}
