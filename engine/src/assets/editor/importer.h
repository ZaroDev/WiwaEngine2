#pragma once

#if !WI_SHIPPING

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "assets/asset_types.h"
#include "assets/wasset_io.h"
#include "meta_file.h"

namespace wiwa
{
	class AssetDatabase;

	// One cooked asset an importer produced; written to Library/ by the database.
	struct EmittedAsset
	{
		AssetRef Ref{};
		AssetType Type = AssetType::None;
		std::vector<std::byte> Payload{};
		std::vector<AssetGuid> Dependencies{}; // resolved guids
	};

	// What an importer sees while cooking one source file.
	class ImportContext
	{
	public:
		ImportContext(AssetDatabase& database, std::filesystem::path source, MetaFile& meta)
			: m_Database(database), m_Source(std::move(source)), m_Meta(meta) {}

		const std::filesystem::path& source() const { return m_Source; }
		const nlohmann::json& settings() const { return m_Meta.Settings; }

		// The whole-file asset (local id 0): a texture's image, a model's prefab, ...
		AssetRef main_asset() const { return AssetRef{ m_Meta.Guid, 0 }; }

		// A sub-asset by name. Its local id is allocated once and kept in the .meta, so the guid survives re-imports.
		AssetRef sub_asset(const std::string& name);

		// Imports another source file (a texture a model uses) if needed and returns its asset. The path may be
		// relative to this source's folder. nullopt if there is no importer for it.
		std::optional<AssetRef> import_dependency(const std::filesystem::path& file);

		template<typename T>
		void emit(AssetRef ref, const T& data, const std::vector<AssetRef>& dependencies = {})
		{
			EmittedAsset asset;
			asset.Ref = ref;
			asset.Type = asset_type_of<T>();
			asset.Payload = serialize(data);
			for (const AssetRef& dependency : dependencies)
			{
				if (dependency.valid())
				{
					asset.Dependencies.push_back(dependency.resolved());
				}
			}
			m_Emitted.push_back(std::move(asset));
		}

		void warn(const std::string& message);

		std::vector<EmittedAsset>& emitted() { return m_Emitted; }

	private:
		AssetDatabase& m_Database;
		std::filesystem::path m_Source;
		MetaFile& m_Meta;
		std::vector<EmittedAsset> m_Emitted{};
	};

	class IImporter
	{
	public:
		virtual ~IImporter() = default;

		virtual const char* name() const = 0;
		// Bump it when the cooked output changes so existing Library files are re-cooked.
		virtual u32 version() const = 0;
		// Lowercase, with the dot: ".png"
		virtual std::vector<std::string> extensions() const = 0;
		virtual nlohmann::json default_settings() const { return nlohmann::json::object(); }

		// Cooks ctx.source(); returns false (after logging why) if it cannot.
		virtual bool import(ImportContext& ctx) = 0;
	};
}

#endif
