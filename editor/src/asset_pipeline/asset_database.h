#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <unordered_map>

#include "assets/job_system.h"
#include "importer.h"

namespace wiwa
{
	// Editor-only. Knows every source file under Assets/, its guid, and whether its cooked files in Library/ are up to date.
	class AssetDatabase
	{
	public:
		AssetDatabase(std::filesystem::path assetsRoot, std::filesystem::path libraryRoot);
		~AssetDatabase();

		void register_importer(Scope<IImporter> importer);

		// Imports everything under Assets/ that is new or changed, on the worker pool, and waits for it.
		void scan();

		// Re-cooks one source file if its content or settings changed.
		void reimport(const std::filesystem::path& source);

		// Imports the file first if it is stale; returns its asset, or nullopt if there is no importer for it.
		// Safe to call from importers running on the worker pool.
		// defaultSettings replace the importer's defaults, but only when the file gets its .meta now.
		std::optional<AssetRef> ensure_imported(const std::filesystem::path& source, const nlohmann::json* defaultSettings = nullptr);

		std::optional<std::filesystem::path> path_of(AssetGuid guid) const;
		bool has_importer_for(const std::filesystem::path& source) const;

		// Called (on a worker thread) with each cooked guid after a file was re-cooked with new content.
		void add_reimport_listener(std::function<void(AssetGuid)> listener);

		const std::filesystem::path& get_assets_root() const { return m_AssetsRoot; }
		const std::filesystem::path& get_library_root() const { return m_LibraryRoot; }

	private:
		enum class SourceState { InFlight, Done, Failed };
		struct SourceRecord
		{
			SourceState State = SourceState::Done;
			AssetGuid Guid{};
		};

		IImporter* find_importer(const std::filesystem::path& source) const;
		std::optional<AssetRef> import_source(const std::filesystem::path& source, bool recheck, const nlohmann::json* defaultSettings);
		std::optional<AssetRef> do_import(const std::filesystem::path& source, const std::string& key, const nlohmann::json* defaultSettings, bool& failed);
		std::filesystem::path cooked_path(AssetGuid guid) const;
		void load_cache();
		void save_cache_locked();

		std::filesystem::path m_AssetsRoot;
		std::filesystem::path m_LibraryRoot;

		std::vector<Scope<IImporter>> m_Importers{};
		std::unordered_map<std::string, IImporter*> m_ImporterByExtension{};

		mutable std::mutex m_Mutex{};
		std::condition_variable m_Condition{};
		std::unordered_map<std::string, SourceRecord> m_Sources{};
		std::unordered_map<AssetGuid, std::string> m_PathByGuid{};
		nlohmann::json m_ImportCache = nlohmann::json::object();
		std::vector<std::function<void(AssetGuid)>> m_Listeners{};

		std::atomic<u32> m_Imported{ 0 };
		std::atomic<u32> m_UpToDate{ 0 };
		std::atomic<u32> m_Failed{ 0 };

		JobSystem m_Jobs{ 4 };
	};
}
