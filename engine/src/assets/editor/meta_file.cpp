#include <pch.h>
#include "meta_file.h"

#if !WI_SHIPPING

#include <fstream>

namespace wiwa
{
	std::filesystem::path MetaFile::meta_path_for(const std::filesystem::path& source)
	{
		std::filesystem::path meta = source;
		meta += ".meta";
		return meta;
	}

	std::optional<MetaFile> MetaFile::load(const std::filesystem::path& metaPath)
	{
		std::ifstream file(metaPath);
		if (!file)
		{
			return std::nullopt;
		}

		try
		{
			nlohmann::json json = nlohmann::json::parse(file);

			MetaFile meta;
			std::optional<AssetGuid> guid = AssetGuid::from_string(json.at("guid").get<std::string>());
			if (!guid || !guid->valid())
			{
				return std::nullopt;
			}

			meta.Guid = *guid;
			meta.Importer = json.value("importer", std::string{});
			meta.Version = json.value("version", 0u);
			meta.Settings = json.value("importSettings", nlohmann::json::object());
			if (!meta.Settings.is_object())
			{
				meta.Settings = nlohmann::json::object();
			}

			if (json.contains("subAssets") && json["subAssets"].is_object())
			{
				for (auto& [name, id] : json["subAssets"].items())
				{
					meta.SubAssets[name] = id.get<u64>();
				}
			}

			return meta;
		}
		catch (const nlohmann::json::exception&)
		{
			return std::nullopt;
		}
	}

	bool MetaFile::save(const std::filesystem::path& metaPath) const
	{
		nlohmann::json json;
		json["guid"] = Guid.to_string();
		json["importer"] = Importer;
		json["version"] = Version;
		json["importSettings"] = Settings;
		json["subAssets"] = nlohmann::json::object();
		for (const auto& [name, id] : SubAssets)
		{
			json["subAssets"][name] = id;
		}

		std::filesystem::path temp = metaPath;
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
		std::filesystem::rename(temp, metaPath, error);
		return !error;
	}
}

#endif
