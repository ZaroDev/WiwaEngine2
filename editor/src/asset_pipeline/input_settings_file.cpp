#include <editor_common.h>
#include "input_settings_file.h"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace wiwa
{
	bool input_settings_from_text(const std::string& text, InputProjectSettings& out, std::string& error)
	{
		using Json = nlohmann::json;
		const Json root = Json::parse(text, nullptr, false);
		if (root.is_discarded() || !root.is_object())
		{
			error = "not a JSON object";
			return false;
		}

		InputProjectSettings settings;
		if (const auto map = root.find("defaultMap"); map != root.end())
		{
			if (!map->is_string())
			{
				error = "\"defaultMap\" must be a guid string";
				return false;
			}

			const std::string guid = map->get<std::string>();
			if (!guid.empty())
			{
				const std::optional<AssetGuid> parsed = AssetGuid::from_string(guid);
				if (!parsed)
				{
					error = "\"defaultMap\" is not a guid: " + guid;
					return false;
				}
				settings.DefaultMap.Guid = *parsed;
			}
		}

		out = settings;
		return true;
	}

	std::string input_settings_to_text(const InputProjectSettings& settings)
	{
		nlohmann::json root;
		root["defaultMap"] = settings.DefaultMap.valid() ? settings.DefaultMap.Guid.to_string() : std::string();
		return root.dump(2);
	}

	bool load_input_settings(const std::filesystem::path& path, InputProjectSettings& out, std::string& error)
	{
		out = InputProjectSettings{};
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
		if (!input_settings_from_text(text.str(), out, error))
		{
			out = InputProjectSettings{};
			return false;
		}
		return true;
	}

	bool save_input_settings(const std::filesystem::path& path, const InputProjectSettings& settings, std::string& error)
	{
		std::filesystem::path temp = path;
		temp += ".tmp";
		{
			std::ofstream file(temp, std::ios::binary | std::ios::trunc);
			file << input_settings_to_text(settings);
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
