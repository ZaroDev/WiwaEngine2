#pragma once

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
		~ImportContext();
		ImportContext(const ImportContext&) = delete;
		ImportContext& operator=(const ImportContext&) = delete;

		const std::filesystem::path& source() const { return m_Source; }
		const nlohmann::json& settings() const { return m_Meta.Settings; }

		// For an importer that keeps something of its own in the .meta (the model importer remembers which file each material
		// went to). Saved with the .meta when the import succeeds.
		nlohmann::json& mutable_settings() { return m_Meta.Settings; }

		// Sub-asset names starting with prefix are dropped from the .meta (an importer stops producing them). True if there were any.
		bool drop_sub_assets(const std::string& prefix);

		// Names an asset this import emits but has not written yet, so that a file imported meanwhile (a model's material, which
		// refers to the model's embedded textures) accepts a reference to it. Forgotten when this context ends.
		void declare_pending_asset(AssetGuid guid);

		// The whole-file asset (local id 0): a texture's image, a model's prefab, ...
		AssetRef main_asset() const { return AssetRef{ m_Meta.Guid, 0 }; }

		// A sub-asset by name. Its local id is allocated once and kept in the .meta, so the guid survives re-imports.
		AssetRef sub_asset(const std::string& name);

		// A sub-asset with a local id chosen by the importer (the cooked physics shapes of a mesh are the mesh's id plus an offset). The id is
		// recorded in the .meta like the others; sub_asset never hands out an id in the range of these.
		AssetRef sub_asset_with_id(const std::string& name, u64 id);

		// Imports another source file (a texture a model uses) if needed and returns its asset. The path may be
		// relative to this source's folder. nullopt if there is no importer for it.
		// defaultSettings only apply if that file has no .meta yet (e.g. a model asks for a normal map to be imported as one).
		std::optional<AssetRef> import_dependency(const std::filesystem::path& file, const nlohmann::json* defaultSettings = nullptr);

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

		// A file the cooked output depends on besides the source itself (a shader's #includes). Its content is hashed into
		// the import record, so the asset is cooked again when the file changes, even while the editor was closed.
		void add_source_dependency(const std::filesystem::path& file) { m_SourceDependencies.push_back(file); }

		// The folders of source assets (Assets/, engine/); shader includes are looked up in <root>/shaders.
		std::vector<std::filesystem::path> get_source_roots() const;

		// Whether the database knows the asset's source file (a reference may name an asset that was deleted).
		bool asset_exists(const AssetRef& ref) const;

		// The source file of an asset the database knows.
		std::optional<std::filesystem::path> path_of(AssetGuid guid) const;

		// A whole-file asset as it was cooked last (a material reads the parameter table of its shader). nullopt if it is not cooked.
		template<typename T>
		std::optional<T> load_cooked(AssetRef ref) const
		{
			std::optional<std::vector<std::byte>> payload = read_cooked_data(ref.resolved(), asset_type_of<T>());
			return payload ? deserialize<T>(*payload) : std::nullopt;
		}

		void warn(const std::string& message);

		std::vector<EmittedAsset>& emitted() { return m_Emitted; }
		const std::vector<std::filesystem::path>& get_source_dependencies() const { return m_SourceDependencies; }

	private:
		std::optional<std::vector<std::byte>> read_cooked_data(AssetGuid guid, AssetType type) const;

		AssetDatabase& m_Database;
		std::filesystem::path m_Source;
		MetaFile& m_Meta;
		std::vector<EmittedAsset> m_Emitted{};
		std::vector<std::filesystem::path> m_SourceDependencies{};
		std::vector<AssetGuid> m_PendingAssets{};
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
		// True for importers that import other source files as dependencies (a model's textures): the scan runs them first,
		// so that the textures are created with the settings the model asks for.
		virtual bool imports_dependencies() const { return false; }
		virtual nlohmann::json default_settings() const { return nlohmann::json::object(); }

		// Cooks ctx.source(); returns false (after logging why) if it cannot.
		virtual bool import(ImportContext& ctx) = 0;
	};
}
