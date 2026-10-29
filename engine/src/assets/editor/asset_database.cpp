#include <pch.h>
#include "asset_database.h"

#if !WI_SHIPPING

#include <fstream>

#include "assets/library_source.h"
#include "assets/pak_format.h"

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
		load_cache();
	}

	AssetDatabase::~AssetDatabase() = default;

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
		return found == m_ImporterByExtension.end() ? nullptr : found->second;
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

	std::filesystem::path AssetDatabase::cooked_path(AssetGuid guid) const
	{
		return LibrarySource(m_LibraryRoot).path_of(guid);
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

	void AssetDatabase::scan()
	{
		std::error_code error;
		std::filesystem::create_directories(m_AssetsRoot, error);

		std::vector<std::filesystem::path> sources;
		auto it = std::filesystem::recursive_directory_iterator(m_AssetsRoot, std::filesystem::directory_options::skip_permission_denied, error);
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

		// Models first: they import their textures with the settings they need (a normal map as a normal map).
		for (const bool dependents : { true, false })
		{
			std::vector<std::future<void>> jobs;
			for (const std::filesystem::path& source : sources)
			{
				if (find_importer(source)->imports_dependencies() == dependents)
				{
					jobs.push_back(m_Jobs.submit([this, source]() { ensure_imported(source); }));
				}
			}

			for (std::future<void>& job : jobs)
			{
				job.get();
			}
		}

		WI_ASSET_LOG(Info, "Asset scan: {0} imported, {1} up to date, {2} failed", m_Imported.exchange(0), m_UpToDate.exchange(0), m_Failed.exchange(0));
	}

	void AssetDatabase::reimport(const std::filesystem::path& source)
	{
		if (find_importer(source))
		{
			import_source(source, true, nullptr);
		}
	}

	std::optional<AssetRef> AssetDatabase::ensure_imported(const std::filesystem::path& source, const nlohmann::json* defaultSettings)
	{
		return import_source(source, false, defaultSettings);
	}

	std::optional<AssetRef> AssetDatabase::import_source(const std::filesystem::path& source, bool recheck, const nlohmann::json* defaultSettings)
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
		std::optional<AssetRef> result = do_import(source, key, defaultSettings, failed);

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

	std::optional<AssetRef> AssetDatabase::do_import(const std::filesystem::path& source, const std::string& key, const nlohmann::json* defaultSettings, bool& failed)
	{
		IImporter* importer = find_importer(source);
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
		const std::string settingsHash = hex(hash64(settingsText.data(), settingsText.size()));
		const std::string guidText = meta.Guid.to_string();

		nlohmann::json previous;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			previous = m_ImportCache.value(guidText, nlohmann::json::object());
		}

		bool upToDate = previous.is_object() && previous.value("sourceHash", "") == sourceHash && previous.value("settingsHash", "") == settingsHash
			&& previous.value("version", 0u) == importer->version() && previous.contains("assets");
		if (upToDate)
		{
			for (const nlohmann::json& asset : previous["assets"])
			{
				std::optional<AssetGuid> cooked = AssetGuid::from_string(asset.get<std::string>());
				std::error_code error;
				if (!cooked || !std::filesystem::exists(cooked_path(*cooked), error))
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

		ImportContext context(*this, source, meta);
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
			if (!write_wasset(cooked_path(cooked), header, asset.Payload))
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
					std::filesystem::remove(cooked_path(*stale), error);
				}
			}
		}

		meta.Version = importer->version();
		meta.save(metaPath);

		std::vector<std::function<void(AssetGuid)>> listeners;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			nlohmann::json entry;
			entry["source"] = key;
			entry["sourceHash"] = sourceHash;
			entry["settingsHash"] = settingsHash;
			entry["version"] = importer->version();
			entry["assets"] = cookedGuids;
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

#endif
