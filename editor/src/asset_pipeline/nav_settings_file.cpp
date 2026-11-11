#include <editor_common.h>
#include "nav_settings_file.h"

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
		if (const auto costs = root.find("areaCost"); costs != root.end())
		{
			if (!costs->is_object())
			{
				error = "\"areaCost\" must be an object of area names and costs";
				return false;
			}
			for (const auto& [name, value] : costs->items())
			{
				int found = -1;
				for (u32 i = 0; i < NAV_AREA_COUNT; i++)
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
			}
		}

		out = sanitized(settings);
		return true;
	}

	std::string nav_settings_to_text(const NavSettings& settings)
	{
		nlohmann::json root;
		nlohmann::json costs = nlohmann::json::object();
		for (u32 i = 0; i < NAV_AREA_COUNT; i++)
		{
			costs[nav_area_name(static_cast<NavArea>(i))] = settings.AreaCost[i];
		}
		root["areaCost"] = costs;
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
		std::filesystem::path temp = path;
		temp += ".tmp";
		{
			std::ofstream file(temp, std::ios::binary | std::ios::trunc);
			file << nav_settings_to_text(settings);
			if (!file)
			{
				error = "cannot write " + temp.string();
				return false;
			}
		}
		std::error_code rename;
		std::filesystem::rename(temp, path, rename);
		if (rename)
		{
			error = "cannot replace " + path.string();
			return false;
		}
		return true;
	}
}
