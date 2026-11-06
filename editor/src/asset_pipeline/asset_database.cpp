#include <editor_common.h>
#include "asset_database.h"

#include <fstream>

#include "assets/asset_catalog.h"
#include "assets/library_source.h"
#include "assets/pak_format.h"
#include "lua_stubs.h"

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	namespace
	{
		std::string lower_extension(const std::filesystem::path& path)
		{
			std::string extension = path.extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return extension;
		}

		std::string key_of(const std::filesystem::path& path)
		{
			std::error_code error;
			std::filesystem::path normal = std::filesystem::weakly_canonical(path, error);
			return (error ? path.lexically_normal() : normal).generic_string();
		}

		std::string hex(u64 value)
		{
			return fmt::format("{:016x}", value);
		}

		bool read_file(const std::filesystem::path& path, std::vector<std::byte>& bytes)
		{
			std::ifstream file(path, std::ios::binary | std::ios::ate);
			if (!file)
			{
				return false;
			}

			const std::streamsize size = file.tellg();
			bytes.resize(static_cast<size_t>(size));
			file.seekg(0);
			return size == 0 || static_cast<bool>(file.read(reinterpret_cast<char*>(bytes.data()), size));
		}
	}

	AssetDatabase::AssetDatabase(std::filesystem::path assetsRoot, std::filesystem::path libraryRoot)
		: m_AssetsRoot(std::move(assetsRoot)), m_LibraryRoot(std::move(libraryRoot))
	{
		m_Roots.push_back({ m_AssetsRoot, m_LibraryRoot });
		load_cache();
	}

	AssetDatabase::~AssetDatabase()
	{
		{
			std::lock_guard<std::mutex> lock(m_ScanMutex);
			if (m_ScanProgress)
			{
				m_ScanProgress->request_cancel();
			}
		}

		if (m_ScanThread.joinable())
		{
			m_ScanThread.join();
		}
	}

	void AssetDatabase::register_importer(Scope<IImporter> importer)
	{
		for (const std::string& extension : importer->extensions())
		{
			m_ImporterByExtension[extension] = importer.get();
		}
		m_Importers.push_back(std::move(importer));
	}

	IImporter* AssetDatabase::find_importer(const std::filesystem::path& source) const
	{
		auto found = m_ImporterByExtension.find(lower_extension(source));
		if (found == m_ImporterByExtension.end() || is_lua_stub_file(source))
		{
			return nullptr; // a LuaLS declaration file next to the scripts is editor support, not an asset
		}
		return found->second;
	}

	bool AssetDatabase::has_importer_for(const std::filesystem::path& source) const
	{
		return find_importer(source) != nullptr;
	}

	void AssetDatabase::add_reimport_listener(std::function<void(AssetGuid)> listener)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_Listeners.push_back(std::move(listener));
	}

	std::optional<std::filesystem::path> AssetDatabase::path_of(AssetGuid guid) const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto found = m_PathByGuid.find(guid);
		if (found == m_PathByGuid.end())
		{
			return std::nullopt;
		}
		return std::filesystem::path(found->second);
	}

	std::optional<std::vector<std::byte>> AssetDatabase::read_cooked_payload(AssetGuid guid, AssetType type) const
	{
		const std::optional<std::filesystem::path> source = path_of(guid);
		if (!source)
		{
			return std::nullopt;
		}

		std::vector<std::byte> bytes;
		if (!read_file(cooked_path(guid, library_folder_for(*source)), bytes))
		{
			return std::nullopt;
		}

		std::span<const std::byte> payload;
		const std::optional<WAssetHeader> header = parse_wasset_header(bytes, payload);
		if (!header || header->Type != type || header->Guid != guid)
		{
			return std::nullopt;
		}
		return std::vector<std::byte>(payload.begin(), payload.end());
	}

	void AssetDatabase::add_pending_asset(AssetGuid guid)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		m_PendingAssets[guid]++;
	}

	void AssetDatabase::remove_pending_asset(AssetGuid guid)
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		auto found = m_PendingAssets.find(guid);
		if (found != m_PendingAssets.end() && --found->second == 0)
		{
			m_PendingAssets.erase(found);
		}
	}

	bool AssetDatabase::is_pending_asset(AssetGuid guid) const
	{
		std::lock_guard<std::mutex> lock(m_Mutex);
		return m_PendingAssets.contains(guid);
	}

	std::unordered_map<AssetGuid, std::string> AssetDatabase::cooked_sources() const
	{
		std::unordered_map<AssetGuid, std::string> sources;
		std::lock_guard<std::mutex> lock(m_Mutex);
		for (auto& [guidText, entry] : m_ImportCache.items())
		{
			if (!entry.is_object() || !entry.contains("assets"))
			{
				continue;
			}
			const std::string source = entry.value("source", std::string{});
			for (const nlohmann::json& asset : entry["assets"])
			{
				if (const std::optional<AssetGuid> guid = asset.is_string() ? AssetGuid::from_string(asset.get<std::string>()) : std::nullopt)
				{
					sources[*guid] = source;
				}
			}
		}
		return sources;
	}

	bool AssetDatabase::guid_exists(AssetGuid guid) const
	{
		return path_of(guid).has_value() || is_pending_asset(guid) || is_cooked_asset(guid);
	}

	bool AssetDatabase::is_cooked_asset(AssetGuid guid) const
	{
		const std::string text = guid.to_string();
		std::lock_guard<std::mutex> lock(m_Mutex);
		for (auto it = m_ImportCache.begin(); it != m_ImportCache.end(); ++it)
		{
			const nlohmann::json& assets = it.value().is_object() ? it.value().value("assets", nlohmann::json::array()) : nlohmann::json::array();
			if (std::find(assets.begin(), assets.end(), text) != assets.end())
			{
				return true;
			}
		}
		return false;
	}

	void AssetDatabase::add_root(std::filesystem::path sourceRoot, std::filesystem::path libraryFolder)
	{
		m_Roots.push_back({ std::move(sourceRoot), std::move(libraryFolder) });
	}

	std::filesystem::path AssetDatabase::cooked_path(AssetGuid guid, const std::filesystem::path& libraryFolder) const
	{
		const std::string name = guid.to_string();
		return libraryFolder / name.substr(0, 2) / (name + ".wasset");
	}

	// The Library folder of the source root a file is under (the first root's for anything outside all of them).
	std::filesystem::path AssetDatabase::library_folder_for(const std::filesystem::path& source) const
	{
		const std::string key = key_of(source);
		for (const SourceRoot& root : m_Roots)
		{
			const std::string prefix = key_of(root.Source) + "/";
			if (key.starts_with(prefix))
			{
				return root.Library;
			}
		}
		return m_Roots.front().Library;
	}

	void AssetDatabase::load_cache()
	{
		std::ifstream file(m_LibraryRoot / "import_cache.json");
		if (!file)
		{
			return;
		}

		try
		{
			nlohmann::json json = nlohmann::json::parse(file);
			if (json.is_object())
			{
				m_ImportCache = std::move(json);
			}
		}
		catch (const nlohmann::json::exception&)
		{
			WI_ASSET_LOG(Warning, "import_cache.json is damaged, everything will be re-imported");
		}
	}

	void AssetDatabase::save_cache_locked()
	{
		std::error_code error;
		std::filesystem::create_directories(m_LibraryRoot, error);

		const std::filesystem::path path = m_LibraryRoot / "import_cache.json";
		std::filesystem::path temp = path;
		temp += ".tmp";
		{
			std::ofstream file(temp, std::ios::trunc);
			file << m_ImportCache.dump(1);
		}
		std::filesystem::rename(temp, path, error);
	}

	Ref<JobProgress> AssetDatabase::scan_async()
	{
		std::lock_guard<std::mutex> lock(m_ScanMutex);
		if (m_ScanProgress && !m_ScanProgress->finished())
		{
			return m_ScanProgress;
		}

		if (m_ScanThread.joinable())
		{
			m_ScanThread.join(); // the previous scan has finished
		}

		Ref<JobProgress> progress = CreateRef<JobProgress>();
		m_ScanProgress = progress;
		// Its own thread: it waits for the pool's jobs, so it must not be one of the pool's workers.
		m_ScanThread = std::thread([this, progress]() { scan_impl(*progress); });
		return progress;
	}

	void AssetDatabase::scan()
	{
		scan_async()->wait();
	}

	void AssetDatabase::scan_impl(JobProgress& progress)
	{
		progress.begin("Finding assets", 0);

		std::vector<std::filesystem::path> sources;
		for (const SourceRoot& root : m_Roots)
		{
			std::error_code error;
			std::filesystem::create_directories(root.Source, error);

			auto it = std::filesystem::recursive_directory_iterator(root.Source, std::filesystem::directory_options::skip_permission_denied, error);
			for (; !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
			{
				if (it->is_directory() && it->path().filename().string().starts_with("."))
				{
					it.disable_recursion_pending();
					continue;
				}

				if (it->is_regular_file() && find_importer(it->path()))
				{
					sources.push_back(it->path());
				}
			}
		}

		progress.begin("Importing assets", static_cast<u32>(sources.size()));

		// Models first: they import their textures with the settings they need (a normal map as a normal map).
		for (const bool dependents : { true, false })
		{
			std::vector<std::future<void>> jobs;
			for (const std::filesystem::path& source : sources)
			{
				if (find_importer(source)->imports_dependencies() == dependents)
				{
					jobs.push_back(m_Jobs.submit([this, source, &progress]()
					{
						if (!progress.cancel_requested())
						{
							ensure_imported(source);
						}
						progress.advance(source.filename().string());
					}));
				}
			}

			for (std::future<void>& job : jobs)
			{
				job.get();
			}
		}

		if (!progress.cancel_requested())
		{
			progress.set_stage("Resolving references");
			if (const u32 refreshed = refresh_unresolved())
			{
				WI_ASSET_LOG(Info, "Cooked {0} asset file(s) again: they refer to assets that were not known the first time", refreshed);
			}
			progress.set_stage("Removing deleted assets");
			const u32 removed = remove_orphans();
			if (removed > 0)
			{
				WI_ASSET_LOG(Info, "Removed {0} cooked file(s) of deleted or unknown sources", removed);
			}
		}

		write_catalog();

		const u32 imported = m_Imported.exchange(0);
		const u32 upToDate = m_UpToDate.exchange(0);
		const u32 failed = m_Failed.exchange(0);
		WI_ASSET_LOG(Info, "Asset scan: {0} imported, {1} up to date, {2} failed{3}", imported, upToDate, failed, progress.cancel_requested() ? " (cancelled)" : "");
		progress.finish(failed == 0 && !progress.cancel_requested());
	}

	// Library/catalog.wcat: which source path names which asset, so that code can say "engine/shaders/imgui.hlsl" instead of a
	// guid. The paths start with the name of the source root, as the root was given ("assets", "engine").
	void AssetDatabase::write_catalog()
	{
		AssetCatalog catalog;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			for (auto& [guidText, entry] : m_ImportCache.items())
			{
				const std::optional<AssetGuid> guid = AssetGuid::from_string(guidText);
				const std::string source = entry.value("source", std::string{});
				std::error_code error;
				if (!guid || source.empty() || !std::filesystem::exists(source, error))
				{
					continue;
				}

				for (const SourceRoot& root : m_Roots)
				{
					const std::string prefix = key_of(root.Source) + "/";
					if (source.starts_with(prefix))
					{
						std::string name = root.Source.generic_string();
						while (name.ends_with("/"))
						{
							name.pop_back();
						}
						catalog.Entries.push_back({ name + "/" + source.substr(prefix.size()), *guid });
						break;
					}
				}
			}
		}

		std::sort(catalog.Entries.begin(), catalog.Entries.end(), [](const auto& a, const auto& b) { return a.Path < b.Path; });
		const std::vector<std::byte> bytes = serialize(catalog);

		// Only written when it changed, so nothing downstream sees a new file for nothing.
		const std::filesystem::path path = m_LibraryRoot / CATALOG_FILE_NAME;
		std::vector<std::byte> existing;
		if (read_file(path, existing) && existing == bytes)
		{
			return;
		}

		std::error_code error;
		std::filesystem::create_directories(m_LibraryRoot, error);
		std::filesystem::path temp = path;
		temp += ".tmp";
		{
			std::ofstream file(temp, std::ios::binary | std::ios::trunc);
			file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		}
		std::filesystem::rename(temp, path, error);
	}

	// The sources whose last import read this file besides their own content.
	std::vector<std::string> AssetDatabase::find_dependents(const std::string& sourceKey) const
	{
		std::vector<std::string> dependents;
		std::lock_guard<std::mutex> lock(m_Mutex);
		for (auto& [guid, entry] : m_ImportCache.items())
		{
			auto extra = entry.find("extraSources");
			if (extra != entry.end() && extra->is_object() && extra->contains(sourceKey))
			{
				dependents.push_back(entry.value("source", std::string{}));
			}
		}
		return dependents;
	}

	// Cooked files whose source file is gone are deleted, so they do not pile up in the Library or end up in a pack.
	u32 AssetDatabase::remove_orphans()
	{
		std::vector<std::string> orphans;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			for (auto& [guid, entry] : m_ImportCache.items())
			{
				const std::string source = entry.value("source", std::string{});
				std::error_code error;
				if (!source.empty() && !std::filesystem::exists(source, error) && !error)
				{
					orphans.push_back(guid);
				}
			}
		}

		for (const std::string& guidText : orphans)
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			const nlohmann::json entry = m_ImportCache[guidText];
			const std::filesystem::path library = library_folder_for(entry.value("source", std::string{}));
			for (const nlohmann::json& asset : entry.value("assets", nlohmann::json::array()))
			{
				if (std::optional<AssetGuid> cooked = AssetGuid::from_string(asset.get<std::string>()))
				{
					std::error_code error;
					std::filesystem::remove(cooked_path(*cooked, library), error);
				}
			}

			if (std::optional<AssetGuid> guid = AssetGuid::from_string(guidText))
			{
				m_PathByGuid.erase(*guid);
			}
			m_ImportCache.erase(guidText);
		}

		if (!orphans.empty())
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			save_cache_locked();
		}

		// Cooked files no import record owns (the cache was lost, or an import was interrupted). A file written in the last
		// moments may belong to an import that has not recorded itself yet, so those are left alone.
		std::unordered_set<std::string> known;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			for (auto& [guid, entry] : m_ImportCache.items())
			{
				for (const nlohmann::json& asset : entry.value("assets", nlohmann::json::array()))
				{
					known.insert(asset.get<std::string>());
				}
			}
		}

		u32 strays = 0;
		std::error_code error;
		for (auto it = std::filesystem::recursive_directory_iterator(m_LibraryRoot, error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
		{
			if (!it->is_regular_file() || it->path().extension() != ".wasset" || known.contains(it->path().stem().string()))
			{
				continue;
			}

			std::error_code timeError;
			const auto age = std::filesystem::file_time_type::clock::now() - std::filesystem::last_write_time(it->path(), timeError);
			if (!timeError && age > std::chrono::seconds(10))
			{
				std::error_code removeError;
				if (std::filesystem::remove(it->path(), removeError))
				{
					strays++;
				}
			}
		}

		return static_cast<u32>(orphans.size()) + strays;
	}

	void AssetDatabase::reimport(const std::filesystem::path& source, bool force)
	{
		if (!find_importer(source))
		{
			return;
		}

		import_source(source, true, force, nullptr);
		write_catalog(); // a file that is new has a path now
		refresh_unresolved(); // and files that refer to it were cooked without it

		// Whatever was cooked with this file as an input (a shader that includes it) is checked again; its record holds the
		// hash of this file, so it is cooked again only if the content really changed.
		const std::string key = key_of(source);
		for (const std::string& dependent : find_dependents(key))
		{
			if (dependent != key)
			{
				import_source(dependent, true, false, nullptr);
			}
		}
	}

	bool AssetDatabase::has_resolvable(const nlohmann::json& entry) const
	{
		if (!entry.is_object() || !entry.contains("unresolved") || !entry["unresolved"].is_array())
		{
			return false;
		}
		for (const nlohmann::json& text : entry["unresolved"])
		{
			const std::optional<AssetGuid> guid = text.is_string() ? AssetGuid::from_string(text.get<std::string>()) : std::nullopt;
			if (guid && guid_exists(*guid))
			{
				return true;
			}
		}
		return false;
	}

	u32 AssetDatabase::refresh_unresolved()
	{
		// A file cooked before the asset it refers to was known (or while that was being imported on another thread) left the reference out of its
		// dependencies; cook it again now. That can make a new guid known, so go on until nothing changes.
		u32 refreshed = 0;
		for (int pass = 0; pass < 4; pass++)
		{
			std::vector<std::string> stale;
			{
				std::vector<std::pair<std::string, nlohmann::json>> candidates;
				{
					std::lock_guard<std::mutex> lock(m_Mutex);
					for (auto& [guid, entry] : m_ImportCache.items())
					{
						if (entry.is_object() && entry.contains("unresolved") && !entry["unresolved"].empty())
						{
							candidates.emplace_back(entry.value("source", std::string{}), entry);
						}
					}
				}
				for (const auto& [source, entry] : candidates)
				{
					std::error_code error;
					if (!source.empty() && has_resolvable(entry) && std::filesystem::exists(source, error))
					{
						stale.push_back(source);
					}
				}
			}
			if (stale.empty())
			{
				break;
			}
			for (const std::string& source : stale)
			{
				import_source(source, true, false, nullptr);
				refreshed++;
			}
		}
		return refreshed;
	}

	std::optional<AssetRef> AssetDatabase::ensure_imported(const std::filesystem::path& source, const nlohmann::json* defaultSettings)
	{
		return import_source(source, false, false, defaultSettings);
	}

	std::optional<AssetRef> AssetDatabase::import_source(const std::filesystem::path& source, bool recheck, bool force, const nlohmann::json* defaultSettings)
	{
		if (!find_importer(source))
		{
			return std::nullopt;
		}

		const std::string key = key_of(source);

		{
			std::unique_lock<std::mutex> lock(m_Mutex);
			SourceRecord& record = m_Sources[key];
			m_Condition.wait(lock, [&]() { return record.State != SourceState::InFlight; });

			if (record.Guid.valid() && !recheck)
			{
				return AssetRef{ record.Guid, 0 };
			}

			record.State = SourceState::InFlight;
		}

		bool failed = false;
		std::optional<AssetRef> result = do_import(source, key, force, defaultSettings, failed);

		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			SourceRecord& record = m_Sources[key];
			record.State = failed ? SourceState::Failed : SourceState::Done;
			if (result)
			{
				record.Guid = result->Guid;
			}
		}
		m_Condition.notify_all();

		return result;
	}

	std::optional<AssetRef> AssetDatabase::do_import(const std::filesystem::path& source, const std::string& key, bool force, const nlohmann::json* defaultSettings, bool& failed)
	{
		IImporter* importer = find_importer(source);
		const std::filesystem::path libraryFolder = library_folder_for(source);
		const std::filesystem::path metaPath = MetaFile::meta_path_for(source);

		MetaFile meta;
		bool metaDirty = false;
		if (std::optional<MetaFile> loaded = MetaFile::load(metaPath))
		{
			meta = std::move(*loaded);
		}
		else
		{
			std::error_code error;
			if (std::filesystem::exists(metaPath, error))
			{
				std::filesystem::path backup = metaPath;
				backup += ".bad";
				std::filesystem::copy_file(metaPath, backup, std::filesystem::copy_options::overwrite_existing, error);
				WI_ASSET_LOG(Error, "{0} is damaged (backup: {1}); a new guid is assigned, references to the old one are lost", metaPath.string(), backup.filename().string());
			}
			else
			{
				WI_ASSET_LOG(Info, "New asset {0}", source.generic_string());
			}

			meta.Guid = AssetGuid::generate();
			meta.Settings = defaultSettings ? *defaultSettings : importer->default_settings();
			metaDirty = true;
		}

		if (meta.Importer != importer->name())
		{
			meta.Importer = importer->name();
			metaDirty = true;
		}

		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			auto owner = m_PathByGuid.find(meta.Guid);
			std::error_code error;
			if (owner != m_PathByGuid.end() && owner->second != key && std::filesystem::exists(owner->second, error))
			{
				WI_ASSET_LOG(Error, "{0} has the same guid as {1} (copied with its .meta?); assigning a new guid", key, owner->second);
				meta.Guid = AssetGuid::generate();
				meta.SubAssets.clear();
				metaDirty = true;
			}
			m_PathByGuid[meta.Guid] = key;
		}

		if (metaDirty)
		{
			meta.save(metaPath);
		}

		const AssetRef mainAsset{ meta.Guid, 0 };

		std::vector<std::byte> sourceBytes;
		if (!read_file(source, sourceBytes))
		{
			WI_ASSET_LOG(Error, "Cannot read {0}", source.generic_string());
			m_Failed++;
			failed = true;
			return mainAsset;
		}

		const std::string sourceHash = hex(hash64(sourceBytes.data(), sourceBytes.size()));
		const std::string settingsText = meta.Settings.dump();
		std::string settingsHash = hex(hash64(settingsText.data(), settingsText.size()));
		const std::string guidText = meta.Guid.to_string();

		nlohmann::json previous;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			previous = m_ImportCache.value(guidText, nlohmann::json::object());
		}

		bool upToDate = !force && previous.is_object() && previous.value("sourceHash", "") == sourceHash && previous.value("settingsHash", "") == settingsHash
			&& previous.value("version", 0u) == importer->version() && previous.contains("assets")
			&& previous.contains("unresolved") // an entry from before this was recorded is cooked once more
			&& previous.value("wassetVersion", 0u) == WASSET_VERSION; // the cooked format changed: everything is cooked again
		if (upToDate && has_resolvable(previous))
		{
			upToDate = false; // a reference it dropped as missing exists now
		}
		if (upToDate && previous.contains("extraSources"))
		{
			// the files the last import read besides the source (shader includes) must be unchanged too
			for (auto& [file, storedHash] : previous["extraSources"].items())
			{
				std::vector<std::byte> bytes;
				if (!read_file(file, bytes) || hex(hash64(bytes.data(), bytes.size())) != storedHash.get<std::string>())
				{
					upToDate = false;
					break;
				}
			}
		}

		if (upToDate)
		{
			for (const nlohmann::json& asset : previous["assets"])
			{
				std::optional<AssetGuid> cooked = AssetGuid::from_string(asset.get<std::string>());
				std::error_code error;
				if (!cooked || !std::filesystem::exists(cooked_path(*cooked, libraryFolder), error))
				{
					upToDate = false;
					break;
				}
			}
		}

		if (upToDate)
		{
			m_UpToDate++;
			return mainAsset;
		}

		// Absolute, so that paths an importer builds from it (a model's textures) are not resolved against the source's folder twice.
		ImportContext context(*this, std::filesystem::absolute(source), meta);
		bool imported = false;
		try
		{
			imported = importer->import(context);
		}
		catch (const std::exception& exception)
		{
			WI_ASSET_LOG(Error, "Importing {0} threw: {1}", source.generic_string(), exception.what());
		}

		if (!imported || context.emitted().empty())
		{
			WI_ASSET_LOG(Error, "Import of {0} failed; the previous cooked files are kept", source.generic_string());
			m_Failed++;
			failed = true;
			return mainAsset;
		}

		nlohmann::json cookedGuids = nlohmann::json::array();
		std::vector<AssetGuid> changed;
		for (EmittedAsset& asset : context.emitted())
		{
			const AssetGuid cooked = asset.Ref.resolved();
			WAssetHeader header{ asset.Type, cooked, asset.Dependencies, asset.Payload.size() };
			if (!write_wasset(cooked_path(cooked, libraryFolder), header, asset.Payload))
			{
				WI_ASSET_LOG(Error, "Cannot write {0} for {1}", cooked.to_string(), source.generic_string());
				m_Failed++;
				failed = true;
				return mainAsset;
			}

			cookedGuids.push_back(cooked.to_string());
			changed.push_back(cooked);
		}

		// files of sub-assets the importer no longer produces
		for (const nlohmann::json& old : previous.value("assets", nlohmann::json::array()))
		{
			if (std::find(cookedGuids.begin(), cookedGuids.end(), old) == cookedGuids.end())
			{
				if (std::optional<AssetGuid> stale = AssetGuid::from_string(old.get<std::string>()))
				{
					std::error_code error;
					std::filesystem::remove(cooked_path(*stale, libraryFolder), error);
				}
			}
		}

		meta.Version = importer->version();
		meta.save(metaPath);

		// An importer may add to the settings (the model importer remembers its material files): the next run must find them unchanged.
		const std::string settingsAfter = meta.Settings.dump();
		settingsHash = hex(hash64(settingsAfter.data(), settingsAfter.size()));

		nlohmann::json extraSources = nlohmann::json::object();
		for (const std::filesystem::path& dependency : context.get_source_dependencies())
		{
			std::vector<std::byte> bytes;
			if (read_file(dependency, bytes))
			{
				extraSources[key_of(dependency)] = hex(hash64(bytes.data(), bytes.size()));
			}
		}

		std::vector<std::function<void(AssetGuid)>> listeners;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			nlohmann::json entry;
			entry["source"] = key;
			entry["sourceHash"] = sourceHash;
			entry["wassetVersion"] = WASSET_VERSION;
			entry["settingsHash"] = settingsHash;
			entry["version"] = importer->version();
			entry["assets"] = cookedGuids;
			entry["extraSources"] = extraSources;
			nlohmann::json unresolved = nlohmann::json::array();
			for (AssetGuid guid : context.unresolved())
			{
				unresolved.push_back(guid.to_string());
			}
			entry["unresolved"] = std::move(unresolved);
			m_ImportCache[guidText] = std::move(entry);
			save_cache_locked();
			listeners = m_Listeners;
		}

		m_Imported++;
		WI_ASSET_LOG(Info, "Imported {0} ({1} asset{2})", source.generic_string(), changed.size(), changed.size() == 1 ? "" : "s");

		for (AssetGuid guid : changed)
		{
			for (auto& listener : listeners)
			{
				listener(guid);
			}
		}

		return mainAsset;
	}
}
