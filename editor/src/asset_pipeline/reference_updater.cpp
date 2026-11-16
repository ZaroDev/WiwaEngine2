#include <editor_common.h>
#include "reference_updater.h"
#include <core/file_io.h>

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "asset_database.h"

#include <assets/asset_log.h>

namespace wiwa
{
	namespace
	{
		using json = nlohmann::ordered_json; // the key order of a file stays as it is

		bool holds_scene_references(const std::filesystem::path& file)
		{
			const std::string extension = file.extension().string();
			return extension == ".wscene" || extension == ".wprefab" || extension == ".wuipanel";
		}

		std::optional<json> read_json(const std::filesystem::path& file)
		{
			std::ifstream in(file, std::ios::binary);
			if (!in)
			{
				return std::nullopt;
			}
			std::stringstream text;
			text << in.rdbuf();
			json parsed = json::parse(text.str(), nullptr, false);
			if (parsed.is_discarded())
			{
				return std::nullopt;
			}
			return parsed;
		}

		// Visits every { "guid": "<32 hex>", "localId": n } object; with apply, a legacy one is rewritten. Returns how many were (or would be) rewritten.
		u32 visit(json& node, const std::unordered_map<AssetGuid, AssetGuid>& redirects, bool apply)
		{
			u32 count = 0;
			if (node.is_object())
			{
				if (node.contains("guid") && node["guid"].is_string() && node.contains("localId") && node["localId"].is_number_unsigned())
				{
					if (const std::optional<AssetGuid> guid = AssetGuid::from_string(node["guid"].get<std::string>()))
					{
						const AssetRef ref{ *guid, node["localId"].get<u64>() };
						const auto found = redirects.find(ref.resolved());
						if (ref.LocalId != 0 && found != redirects.end())
						{
							if (apply)
							{
								node["guid"] = found->second.to_string();
								node["localId"] = 0;
							}
							return 1;
						}
					}
				}
				for (auto& [key, value] : node.items())
				{
					count += visit(value, redirects, apply);
				}
			}
			else if (node.is_array())
			{
				for (json& value : node)
				{
					count += visit(value, redirects, apply);
				}
			}
			return count;
		}

		std::vector<std::filesystem::path> source_files(const AssetDatabase& database)
		{
			std::vector<std::filesystem::path> files;
			for (const AssetDatabase::SourceRoot& root : database.get_roots())
			{
				std::error_code error;
				for (std::filesystem::recursive_directory_iterator it(root.Source, std::filesystem::directory_options::skip_permission_denied, error), end; !error && it != end; it.increment(error))
				{
					if (it->is_regular_file(error) && holds_scene_references(it->path()))
					{
						files.push_back(it->path());
					}
				}
			}
			std::sort(files.begin(), files.end());
			return files;
		}
	}

	std::vector<ReferenceUpdate> plan_reference_update(const AssetDatabase& database)
	{
		const std::unordered_map<AssetGuid, AssetGuid> redirects = database.redirect_map();
		std::vector<ReferenceUpdate> plan;
		if (redirects.empty())
		{
			return plan;
		}
		for (const std::filesystem::path& file : source_files(database))
		{
			std::optional<json> content = read_json(file);
			if (!content)
			{
				continue;
			}
			if (const u32 count = visit(*content, redirects, false); count > 0)
			{
				plan.push_back(ReferenceUpdate{ file, count });
			}
		}
		return plan;
	}

	u32 apply_reference_update(const AssetDatabase& database, const std::vector<ReferenceUpdate>& plan)
	{
		const std::unordered_map<AssetGuid, AssetGuid> redirects = database.redirect_map();
		u32 rewritten = 0;
		for (const ReferenceUpdate& item : plan)
		{
			std::optional<json> content = read_json(item.File);
			if (!content)
			{
				WI_ASSET_LOG(Warning, "Cannot read {0}; its references were not updated", item.File.string());
				continue;
			}
			const u32 count = visit(*content, redirects, true);
			if (count == 0)
			{
				continue;
			}

			const std::string text = content->dump(2) + "\n";
			std::string error;
			if (!write_file_atomic(item.File, text.data(), text.size(), error))
			{
				WI_ASSET_LOG(Warning, "Cannot write {0}: {1}; its references were not updated", item.File.string(), error);
				continue;
			}
			rewritten += count;
		}
		WI_ASSET_LOG(Info, "Updated {0} reference(s) to extracted assets", rewritten);
		return rewritten;
	}
}
