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

		// The latest scan's progress without starting one; null before the first scan.
		Ref<JobProgress> scan_progress() const
		{
			std::lock_guard<std::mutex> lock(m_ScanMutex);
			return m_ScanProgress;
		}

		// scan_async() and wait for it. Only for command-line runs and background jobs; never call it from the editor's main thread.
		void scan();

		// Imports the sources of one root (an index of get_roots()) that are stale or not cooked, and waits. For the editor's start: the engine's own
		// assets (the shaders) have to be cooked before the renderer asks for them, or a cold Library (the first run, or after the cooked format changed)
		// would make a pass fail and the editor quit. Not for the main loop.
		void cook_root(size_t rootIndex);

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
		// The importer that cooks this file (extension), or null.
		const IImporter* importer_for(const std::filesystem::path& source) const;

		// Stores the values of the importer's options in the .meta (only the schema's keys, sanitized; whatever else the .meta holds stays). Does not
		// cook: follow it with reimport_async. False with error if the importer is not editable, the file has no .meta, or it is being imported now.
		bool store_import_settings(const std::filesystem::path& source, const nlohmann::json& values, std::string& error);
		// The schema's defaults, stored the same way.
		bool store_default_import_settings(const std::filesystem::path& source, std::string& error);
		// Whether the last import of this source failed.
		bool import_failed(const std::filesystem::path& source) const;

		// Cooks these files again (forced) on the worker pool and returns at once; follow it with the progress. Requests made while a reimport runs are
		// added to it (the same progress comes back, its total grows). Not for the scan: a scan has its own progress.
		Ref<JobProgress> reimport_async(std::vector<std::filesystem::path> sources);
		// The latest reimport_async's progress; null before the first.
		Ref<JobProgress> reimport_progress() const
		{
			std::lock_guard<std::mutex> lock(m_ReimportMutex);
			return m_ReimportProgress;
		}
		// The files whose import failed in the latest reimport_async (one line each), emptied by the call. Call it after the progress finished.
		std::vector<std::string> take_reimport_errors();

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
		// Called (on a worker thread) after the catalog was written with other content than before: new assets, or new redirects.
		void add_catalog_listener(std::function<void()> listener);

		// The redirects of the model imports (legacy resolved guid -> the guid of the file the asset was extracted to), from the import records.
		std::unordered_map<AssetGuid, AssetGuid> redirect_map() const;
		// The reference with a redirect followed once (a reference to a legacy sub-asset becomes one to its extracted file), else the same.
		AssetRef redirected(AssetRef ref) const;
		// The sources (guid of the model) whose import redirected something to target: where a legacy reference to it may still be written.
		std::vector<AssetGuid> redirect_sources(AssetGuid target) const;

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
		// Tells path_of() which source has which guid from the .meta files, before anything is imported: an importer that names another asset by guid
		// (a clip or a skinned mesh naming its skeleton) finds its file even on a cold Library, where nothing has been imported yet.
		void index_meta_guids(const std::vector<std::filesystem::path>& sources);
		std::vector<std::string> find_dependents(const std::string& sourceKey) const;
		// A cache entry that dropped references as missing, and one of them is known now.
		bool has_resolvable(const nlohmann::json& entry) const;
		// Cooks again every file that is in that case; returns how many.
		u32 refresh_unresolved();
		u32 remove_orphans();
		void write_catalog();
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
		std::vector<std::function<void()>> m_CatalogListeners{};

		std::atomic<u32> m_Imported{ 0 };
		std::atomic<u32> m_UpToDate{ 0 };
		std::atomic<u32> m_Failed{ 0 };

		mutable std::mutex m_ScanMutex{};
		Ref<JobProgress> m_ScanProgress{};
		std::thread m_ScanThread{};

		mutable std::mutex m_ReimportMutex{};
		Ref<JobProgress> m_ReimportProgress{};
		std::thread m_ReimportThread{};
		std::vector<std::filesystem::path> m_ReimportPending{};
		std::vector<std::string> m_ReimportErrors{};

		JobSystem m_Jobs{ 4 };
	};
}
