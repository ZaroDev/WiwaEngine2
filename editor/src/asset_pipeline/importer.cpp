#include <editor_common.h>
#include "importer.h"

#include "asset_database.h"

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

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
			next = std::max(next, id + 1);
		}

		m_Meta.SubAssets[name] = next;
		return AssetRef{ m_Meta.Guid, next };
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

	bool ImportContext::asset_exists(const AssetRef& ref) const
	{
		return ref.valid() && (m_Database.path_of(ref.Guid).has_value() || m_Database.is_pending_asset(ref.Guid) || m_Database.is_cooked_asset(ref.Guid));
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

	void ImportContext::warn(const std::string& message)
	{
		WI_ASSET_LOG(Warning, "{0}: {1}", m_Source.filename().string(), message);
	}
}
