#include <editor_common.h>
#include "build_settings.h"

#include <fstream>

#include <nlohmann/json.hpp>

namespace wiwa
{
	namespace
	{
		// An absent key is an empty list; a string that is not a guid fails with a message that names it.
		bool read_guids(const nlohmann::json& json, const char* key, std::vector<AssetGuid>& out, std::string& error)
		{
			if (!json.contains(key))
			{
				return true;
			}
			if (!json[key].is_array())
			{
				error = fmt::format("\"{0}\" is not a list", key);
				return false;
			}

			for (const nlohmann::json& item : json[key])
			{
				std::optional<AssetGuid> guid = item.is_string() ? AssetGuid::from_string(item.get<std::string>()) : std::nullopt;
				if (!guid)
				{
					error = fmt::format("\"{0}\" holds something that is not a guid: {1}", key, item.dump());
					return false;
				}
				out.push_back(*guid);
			}
			return true;
		}

		nlohmann::json guids_to_json(const std::vector<AssetGuid>& guids)
		{
			nlohmann::json list = nlohmann::json::array();
			for (const AssetGuid& guid : guids)
			{
				list.push_back(guid.to_string());
			}
			return list;
		}
	}

	std::optional<BuildSettings> BuildSettings::load(const std::filesystem::path& path, std::string& error)
	{
		error.clear();

		std::ifstream file(path);
		if (!file)
		{
			return std::nullopt;
		}

		try
		{
			const nlohmann::json json = nlohmann::json::parse(file);
			if (!json.is_object())
			{
				error = "the file does not hold an object";
				return std::nullopt;
			}

			BuildSettings settings;
			if (json.contains("defaultScene"))
			{
				std::optional<AssetGuid> guid = json["defaultScene"].is_string() ? AssetGuid::from_string(json["defaultScene"].get<std::string>()) : std::nullopt;
				if (!guid)
				{
					error = fmt::format("\"defaultScene\" is not a guid: {0}", json["defaultScene"].dump());
					return std::nullopt;
				}
				settings.DefaultScene = *guid;
			}

			if (!read_guids(json, "scenes", settings.Scenes, error) || !read_guids(json, "alwaysInclude", settings.AlwaysInclude, error))
			{
				return std::nullopt;
			}
			return settings;
		}
		catch (const nlohmann::json::exception& exception)
		{
			error = exception.what();
			return std::nullopt;
		}
	}

	bool BuildSettings::save(const std::filesystem::path& path) const
	{
		nlohmann::json json;
		json["defaultScene"] = DefaultScene.to_string();
		json["scenes"] = guids_to_json(Scenes);
		json["alwaysInclude"] = guids_to_json(AlwaysInclude);

		std::filesystem::path temp = path;
		temp += ".tmp";
		{
			std::ofstream file(temp, std::ios::trunc);
			if (!file)
			{
				return false;
			}
			file << json.dump(2);
			if (!file)
			{
				return false;
			}
		}

		std::error_code error;
		std::filesystem::rename(temp, path, error);
		return !error;
	}

	bool BuildSettings::validate(std::string& error) const
	{
		if (DefaultScene.valid() && std::find(Scenes.begin(), Scenes.end(), DefaultScene) == Scenes.end())
		{
			error = fmt::format("the default scene {0} is not in the list of scenes to pack", DefaultScene.to_string());
			return false;
		}
		return true;
	}
}
