#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <unordered_map>

#include "assets/job_system.h"
#include "importer.h"
#include <core/job_progress.h>

namespace wiwa
{
	// Editor-only. Knows every source file under Assets/, its guid, and whether its cooked files in Library/ are up to date.
	class AssetDatabase
	{
	public:
		AssetDatabase(std::filesystem::path assetsRoot, std::filesystem::path libraryRoot);
		~AssetDatabase();

		void register_importer(Scope<IImporter> importer);

		// Another folder of source assets, cooked into its own folder of the Library (the engine's own assets: "engine" ->
		// "library/engine"). It behaves exactly like assets/: .meta files, guids, importers, live re-import.
		void add_root(std::filesystem::path sourceRoot, std::filesystem::path libraryFolder);

		// Imports everything under the source roots that is new or changed, in the background, and returns at once. The
		// progress object is updated while it runs. If a scan is already running, that one's progress is returned.
		Ref<JobProgress> scan_async();

		// scan_async() and wait for it. Only for command-line runs and background jobs; never call it from the editor's main thread.
		void scan();

		// Re-cooks one source file if its content or settings changed (or always, with force).
		void reimport(const std::filesystem::path& source, bool force = false);

		// Imports the file first if it is stale; returns its asset, or nullopt if there is no importer for it.
		// Safe to call from importers running on the worker pool.
		// defaultSettings replace the importer's defaults, but only when the file gets its .meta now.
		std::optional<AssetRef> ensure_imported(const std::filesystem::path& source, const nlohmann::json* defaultSettings = nullptr);

		std::optional<std::filesystem::path> path_of(AssetGuid guid) const;

		// The payload of the cooked file of a whole-file asset, nullopt if it is not cooked (never imported, or its first import failed)
		// or is not of the given type.
		std::optional<std::vector<std::byte>> read_cooked_payload(AssetGuid guid, AssetType type) const;
		bool has_importer_for(const std::filesystem::path& source) const;

		// Cooked assets an import in progress is about to write (see ImportContext::declare_pending_asset).
		void add_pending_asset(AssetGuid guid);
		void remove_pending_asset(AssetGuid guid);
		bool is_pending_asset(AssetGuid guid) const;
		// Whether some source's last import cooked this guid (the embedded textures and other sub-assets of a model have no path of their own).
		bool is_cooked_asset(AssetGuid guid) const;
		// The source file each cooked guid came from, sub-assets included (a model's meshes and textures have no path of their own, only this).
		std::unordered_map<AssetGuid, std::string> cooked_sources() const;
		// A source, an asset being imported or a cooked file has this guid.
		bool guid_exists(AssetGuid guid) const;

		// Called (on a worker thread) with each cooked guid after a file was re-cooked with new content.
		void add_reimport_listener(std::function<void(AssetGuid)> listener);

		struct SourceRoot
		{
			std::filesystem::path Source;
			std::filesystem::path Library;
		};
		const std::vector<SourceRoot>& get_roots() const { return m_Roots; }
		const std::filesystem::path& get_assets_root() const { return m_AssetsRoot; }
		const std::filesystem::path& get_library_root() const { return m_LibraryRoot; }

	private:
		enum class SourceState { InFlight, Done, Failed };
		struct SourceRecord
		{
			SourceState State = SourceState::Done;
			AssetGuid Guid{};
		};

		void scan_impl(JobProgress& progress);
		std::vector<std::string> find_dependents(const std::string& sourceKey) const;
		// A cache entry that dropped references as missing, and one of them is known now.
		bool has_resolvable(const nlohmann::json& entry) const;
		// Cooks again every file that is in that case; returns how many.
		u32 refresh_unresolved();
		u32 remove_orphans();
		void write_catalog(); // returns how many sources/files were cleaned up
		IImporter* find_importer(const std::filesystem::path& source) const;
		std::optional<AssetRef> import_source(const std::filesystem::path& source, bool recheck, bool force, const nlohmann::json* defaultSettings);
		std::optional<AssetRef> do_import(const std::filesystem::path& source, const std::string& key, bool force, const nlohmann::json* defaultSettings, bool& failed);
		std::filesystem::path cooked_path(AssetGuid guid, const std::filesystem::path& libraryFolder) const;
		std::filesystem::path library_folder_for(const std::filesystem::path& source) const;
		void load_cache();
		void save_cache_locked();

		std::filesystem::path m_AssetsRoot;
		std::filesystem::path m_LibraryRoot;
		std::vector<SourceRoot> m_Roots{};

		std::vector<Scope<IImporter>> m_Importers{};
		std::unordered_map<std::string, IImporter*> m_ImporterByExtension{};

		mutable std::mutex m_Mutex{};
		std::condition_variable m_Condition{};
		std::unordered_map<std::string, SourceRecord> m_Sources{};
		std::unordered_map<AssetGuid, std::string> m_PathByGuid{};
		std::unordered_map<AssetGuid, u32> m_PendingAssets{};
		nlohmann::json m_ImportCache = nlohmann::json::object();
		std::vector<std::function<void(AssetGuid)>> m_Listeners{};

		std::atomic<u32> m_Imported{ 0 };
		std::atomic<u32> m_UpToDate{ 0 };
		std::atomic<u32> m_Failed{ 0 };

		std::mutex m_ScanMutex{};
		Ref<JobProgress> m_ScanProgress{};
		std::thread m_ScanThread{};

		JobSystem m_Jobs{ 4 };
	};
}
