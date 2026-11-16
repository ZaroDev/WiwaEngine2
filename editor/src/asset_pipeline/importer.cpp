#include <editor_common.h>
#include "importer.h"

#include <assets/asset_log.h>

#include "asset_database.h"
#include "file_utils.h"

namespace wiwa
{
	AssetRef ImportContext::sub_asset(const std::string& name)
	{
		auto found = m_Meta.SubAssets.find(name);
		if (found != m_Meta.SubAssets.end())
		{
			return AssetRef{ m_Meta.Guid, found->second };
		}

		u64 next = 1;
		for (const auto& [_, id] : m_Meta.SubAssets)
		{
			if (id < PHYSICS_HULL_LOCAL_ID_OFFSET) // the ids above are the importer's own (cooked physics shapes)
			{
				next = std::max(next, id + 1);
			}
		}

		m_Meta.SubAssets[name] = next;
		return AssetRef{ m_Meta.Guid, next };
	}

	AssetRef ImportContext::sub_asset_with_id(const std::string& name, u64 id)
	{
		m_Meta.SubAssets[name] = id;
		return AssetRef{ m_Meta.Guid, id };
	}

	std::optional<AssetRef> ImportContext::import_dependency(const std::filesystem::path& file, const nlohmann::json* defaultSettings)
	{
		std::filesystem::path path = file.is_absolute() ? file : m_Source.parent_path() / file;
		return m_Database.ensure_imported(path.lexically_normal(), defaultSettings);
	}

	ImportContext::~ImportContext()
	{
		for (const AssetGuid& guid : m_PendingAssets)
		{
			m_Database.remove_pending_asset(guid);
		}
	}

	bool ImportContext::drop_sub_assets(const std::string& prefix)
	{
		bool any = false;
		for (auto it = m_Meta.SubAssets.begin(); it != m_Meta.SubAssets.end();)
		{
			if (it->first.rfind(prefix, 0) == 0)
			{
				it = m_Meta.SubAssets.erase(it);
				any = true;
			}
			else
			{
				++it;
			}
		}
		return any;
	}

	void ImportContext::declare_pending_asset(AssetGuid guid)
	{
		m_Database.add_pending_asset(guid);
		m_PendingAssets.push_back(guid);
	}

	AssetRef ImportContext::redirected(const AssetRef& ref) const
	{
		return m_Database.redirected(ref);
	}

	std::unordered_map<AssetGuid, AssetGuid> ImportContext::redirect_map() const
	{
		return m_Database.redirect_map();
	}

	bool ImportContext::asset_exists(const AssetRef& ref) const
	{
		if (!ref.valid())
		{
			return false;
		}
		if (m_Database.guid_exists(ref.Guid))
		{
			return true;
		}
		if (std::find(m_Unresolved.begin(), m_Unresolved.end(), ref.Guid) == m_Unresolved.end())
		{
			m_Unresolved.push_back(ref.Guid);
		}
		return false;
	}

	std::vector<std::filesystem::path> ImportContext::get_source_roots() const
	{
		std::vector<std::filesystem::path> roots;
		for (const AssetDatabase::SourceRoot& root : m_Database.get_roots())
		{
			roots.push_back(root.Source);
		}
		return roots;
	}

	std::optional<std::filesystem::path> ImportContext::path_of(AssetGuid guid) const
	{
		return m_Database.path_of(guid);
	}

	std::optional<std::vector<std::byte>> ImportContext::read_cooked_data(AssetGuid guid, AssetType type) const
	{
		return m_Database.read_cooked_payload(guid, type);
	}

	std::optional<std::vector<std::byte>> ImportContext::read_source(bool allowEmpty)
	{
		std::optional<std::vector<std::byte>> bytes = read_binary_file(m_Source);
		if (!bytes || (!allowEmpty && bytes->empty()))
		{
			WI_ASSET_LOG(Error, "Cannot read {0}", m_Source.string());
			return std::nullopt;
		}
		return bytes;
	}

	bool ImportContext::depend_on(AssetGuid guid, const char* what)
	{
		if (const std::optional<std::filesystem::path> path = path_of(guid))
		{
			import_dependency(*path);
			return true;
		}
		warn(std::string(what) + " it names (" + guid.to_string() + ") was not found");
		return false;
	}

	void ImportContext::warn(const std::string& message)
	{
		WI_ASSET_LOG(Warning, "{0}: {1}", m_Source.filename().string(), message);
	}
}
