#include <editor_common.h>
#include "packager.h"

#include <zstd.h>

#include <assets/asset_catalog.h>
#include <assets/build_info.h>
#include <audio/audio_project_settings.h>
#include <assets/pak_format.h>
#include <assets/project_blob.h>
#include <input/input_project_settings.h>
#include <physics/physics_project_settings.h>
#include <assets/wasset_io.h>

#include <functional>
#include <unordered_set>

#include "file_utils.h"

#include <assets/asset_log.h>

namespace wiwa
{
	// 12 is within a few percent of 19 for these data and several times faster; a pack is made by every smoke test.
	constexpr int PAK_ZSTD_LEVEL = 12;

	std::unordered_set<AssetGuid> select_pack_set(const std::vector<PackCandidate>& candidates, const BuildSettings& settings, std::vector<AssetGuid>& missing, const std::unordered_map<AssetGuid, AssetGuid>& redirects)
	{
		missing.clear();
		auto redirected = [&redirects](AssetGuid guid)
		{
			const auto found = redirects.find(guid);
			return found == redirects.end() ? guid : found->second;
		};

		std::unordered_map<AssetGuid, const PackCandidate*> byGuid;
		std::vector<AssetGuid> pending;
		for (const PackCandidate& candidate : candidates)
		{
			byGuid[candidate.Guid] = &candidate;
			if (candidate.IsEngine)
			{
				pending.push_back(candidate.Guid);
			}
		}

		for (const std::vector<AssetGuid>* roots : { &settings.Scenes, &settings.AlwaysInclude })
		{
			for (AssetGuid named : *roots)
			{
				const AssetGuid guid = redirected(named);
				if (byGuid.contains(guid))
				{
					pending.push_back(guid);
				}
				else if (std::find(missing.begin(), missing.end(), guid) == missing.end())
				{
					missing.push_back(guid);
				}
			}
		}

		std::unordered_set<AssetGuid> selected;
		while (!pending.empty())
		{
			const AssetGuid current = pending.back();
			pending.pop_back();

			auto found = byGuid.find(current);
			if (found == byGuid.end())
			{
				// something a packed asset needs has no cooked file: the pack would ship without it
				if (std::find(missing.begin(), missing.end(), current) == missing.end())
				{
					missing.push_back(current);
				}
				continue;
			}
			if (!selected.insert(current).second) // a cycle or a shared asset is met again
			{
				continue;
			}
			for (AssetGuid dependency : found->second->Dependencies)
			{
				pending.push_back(redirected(dependency));
			}
		}
		return selected;
	}

	namespace
	{
		// One settings file that goes into the pack: its blob guid, what the log calls it, and how to turn the file into the
		// blob bytes (false and a reason if it is damaged).
		struct SettingsBlob
		{
			AssetGuid Guid;
			const char* Label;
			const std::filesystem::path* File;
			std::function<bool(const std::filesystem::path&, std::vector<std::byte>&, std::string&)> Load;
		};

		// A loader for load(path, T&, error): the file's T serialised the way a game reads it back.
		template<typename T, typename Load>
		auto settings_loader(Load load)
		{
			return [load](const std::filesystem::path& file, std::vector<std::byte>& bytes, std::string& why)
			{
				T settings;
				if (!load(file, settings, why))
				{
					return false;
				}
				bytes = serialize(settings);
				return true;
			};
		}

		// A source path as the log shows it: relative to the working directory (the editor's project folder) when it is inside.
		std::string shown_path(const std::string& path)
		{
			std::error_code error;
			const std::filesystem::path relative = std::filesystem::path(path).lexically_relative(std::filesystem::current_path(error));
			return (!error && !relative.empty() && *relative.begin() != "..") ? relative.generic_string() : path;
		}
	}

	bool Packager::pack(AssetDatabase& database, const std::filesystem::path& outDirectory, JobProgress* progress, const BuildSettings* settings, const std::filesystem::path& physicsSettingsFile, const std::filesystem::path& inputSettingsFile, const std::filesystem::path& projectSettingsFile, const std::filesystem::path& audioSettingsFile, const std::filesystem::path& navSettingsFile)
	{
		JobProgress local;
		if (progress == nullptr)
		{
			progress = &local;
		}

		// Library/ is brought up to date first, so the pack never holds stale files (this waits for a running import).
		progress->begin("Importing assets", 0);
		database.scan();

		std::vector<std::filesystem::path> files;
		std::error_code error;
		for (auto it = std::filesystem::recursive_directory_iterator(database.get_library_root(), error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
		{
			if (it->is_regular_file() && it->path().extension() == ".wasset")
			{
				files.push_back(it->path());
			}
		}
		std::sort(files.begin(), files.end()); // the same Library gives the same pack

		size_t excluded = 0;
		if (settings != nullptr)
		{
			std::string why;
			if (!settings->validate(why))
			{
				WI_ASSET_LOG(Error, "Packing failed: {0}", why);
				progress->finish(false);
				return false;
			}

			// what each cooked file is and needs, to follow the settings through the dependencies
			std::vector<PackCandidate> candidates;
			std::unordered_map<AssetGuid, std::filesystem::path> fileOf;
			for (const std::filesystem::path& file : files)
			{
				std::optional<std::vector<std::byte>> blob = read_binary_file(file);
				std::span<const std::byte> payload;
				std::optional<WAssetHeader> assetHeader = blob ? parse_wasset_header(*blob, payload) : std::nullopt;
				// A file that cannot be read still counts as that asset (the guid is its name): if the build needs it, packing it fails below.
				const std::optional<AssetGuid> named = AssetGuid::from_string(file.stem().string());
				if (!assetHeader && !named)
				{
					continue;
				}

				PackCandidate candidate;
				candidate.Guid = assetHeader ? assetHeader->Guid : *named;
				candidate.IsAudioClip = assetHeader && assetHeader->Type == AssetType::AudioClip;
				if (assetHeader)
				{
					candidate.Dependencies = assetHeader->Dependencies;
				}
				for (size_t i = 1; i < database.get_roots().size() && !candidate.IsEngine; i++) // root 0 is Assets/, the others are the engine's
				{
					const std::filesystem::path relative = file.lexically_normal().lexically_relative(database.get_roots()[i].Library.lexically_normal());
					candidate.IsEngine = !relative.empty() && *relative.begin() != "..";
				}
				fileOf[candidate.Guid] = file;
				candidates.push_back(std::move(candidate));
			}

			std::vector<AssetGuid> missing;
			// No scene refers to the project's default input map, so it is packed because the project chose it.
			BuildSettings effective = *settings;
			{
				InputProjectSettings inputSettings;
				std::string inputError;
				if (load_input_settings(inputSettingsFile, inputSettings, inputError) && inputSettings.DefaultMap.valid() &&
					std::find(effective.AlwaysInclude.begin(), effective.AlwaysInclude.end(), inputSettings.DefaultMap.Guid) == effective.AlwaysInclude.end())
				{
					effective.AlwaysInclude.push_back(inputSettings.DefaultMap.Guid);
				}

				// Nor does any scene refer to the soundbanks the audio settings load.
				AudioProjectSettings audioSettings;
				std::string audioError;
				if (load_audio_settings(audioSettingsFile, audioSettings, audioError))
				{
					// The MiniAudio backend plays clips by name, so no scene refers to them: every clip goes in.
					if (audioSettings.Backend == AudioBackendKind::MiniAudio)
					{
						for (const PackCandidate& candidate : candidates)
						{
							if (candidate.IsAudioClip && std::find(effective.AlwaysInclude.begin(), effective.AlwaysInclude.end(), candidate.Guid) == effective.AlwaysInclude.end())
							{
								effective.AlwaysInclude.push_back(candidate.Guid);
							}
						}
					}
					for (const AssetRef& bank : audioSettings.Banks)
					{
						if (std::find(effective.AlwaysInclude.begin(), effective.AlwaysInclude.end(), bank.Guid) == effective.AlwaysInclude.end())
						{
							effective.AlwaysInclude.push_back(bank.Guid);
						}
					}
				}
			}
			const std::unordered_set<AssetGuid> selected = select_pack_set(candidates, effective, missing, database.redirect_map());
			if (!missing.empty())
			{
				for (AssetGuid guid : missing)
				{
					WI_ASSET_LOG(Error, "Packing failed: the build needs {0}, which is not in the Library", guid.to_string());
				}
				progress->finish(false);
				return false;
			}

			std::vector<std::filesystem::path> chosen;
			for (AssetGuid guid : selected)
			{
				chosen.push_back(fileOf[guid]);
			}
			std::sort(chosen.begin(), chosen.end());
			excluded = files.size() - chosen.size();
			files = std::move(chosen);
		}

		progress->begin("Compressing assets", static_cast<u32>(files.size()) + 1); // + the catalog

		std::filesystem::create_directories(outDirectory, error);
		const std::filesystem::path target = outDirectory / "assets.wpak";
		std::filesystem::path temp = target;
		temp += ".tmp";

		std::ofstream out(temp, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			WI_ASSET_LOG(Error, "Cannot write {0}", temp.string());
			progress->finish(false);
			return false;
		}

		PakHeader header;
		out.write(reinterpret_cast<const char*>(&header), sizeof(header)); // patched at the end

		// where each cooked guid came from, to name it in the log (the cache also knows the sub-assets of a model)
		const std::unordered_map<AssetGuid, std::string> sourceOf = database.cooked_sources();
		std::vector<PakEntry> index;
		std::unordered_set<AssetGuid> packed; // the assets (not the catalog or the build info) that went in
		u64 offset = sizeof(PakHeader);
		u64 totalRaw = 0;
		bool ok = true;

		// Compresses one blob into the pack and notes it in the index.
		auto add_blob = [&](AssetGuid guid, u32 type, const std::vector<std::byte>& blob, const std::filesystem::path& what) -> bool
		{
			// Zstd, but a blob that does not shrink by at least 5% (audio, video, already packed data) is stored as is: it then also
			// loads without a decoder pass.
			std::vector<char> compressed(ZSTD_compressBound(blob.size()));
			const size_t compressedSize = ZSTD_compress(compressed.data(), compressed.size(), blob.data(), blob.size(), PAK_ZSTD_LEVEL);
			if (ZSTD_isError(compressedSize))
			{
				WI_ASSET_LOG(Error, "Compression of {0} failed", what.string());
				return false;
			}
			const bool stored = compressedSize + blob.size() / 20 >= blob.size();
			if (stored)
			{
				out.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
			}
			else
			{
				out.write(compressed.data(), static_cast<std::streamsize>(compressedSize));
			}

			PakEntry entry;
			entry.Guid = guid;
			entry.Type = type;
			entry.Codec = static_cast<u32>(stored ? PakCodec::Stored : PakCodec::Zstd);
			entry.Offset = offset;
			entry.CompressedSize = stored ? static_cast<u64>(blob.size()) : static_cast<u64>(compressedSize);
			entry.UncompressedSize = blob.size();
			entry.Hash = hash64(blob.data(), blob.size());
			index.push_back(entry);

			offset += entry.CompressedSize;
			totalRaw += entry.UncompressedSize;
			return true;
		};

		// what went into the pack besides cooked assets, named in the log the same way
		auto log_extra = [&](AssetGuid guid, const std::string& what)
		{
			WI_ASSET_LOG(Info, "Pack: {0} {1:<10} {2}", guid.to_string(), "data", what);
		};

		for (const std::filesystem::path& file : files)
		{
			if (progress->cancel_requested())
			{
				out.close();
				std::filesystem::remove(temp, error);
				WI_ASSET_LOG(Warning, "Packing cancelled");
				progress->finish(false);
				return false;
			}

			progress->advance(file.stem().string());

			std::optional<std::vector<std::byte>> blob = read_binary_file(file);
			if (!blob)
			{
				WI_ASSET_LOG(Error, "Cannot read {0}", file.string());
				ok = false;
				continue;
			}

			std::span<const std::byte> payload;
			std::optional<WAssetHeader> assetHeader = parse_wasset_header(*blob, payload);
			if (!assetHeader || assetHeader->Guid.to_string() != file.stem().string())
			{
				WI_ASSET_LOG(Error, "{0} is not a valid cooked asset, skipped", file.string());
				ok = false;
				continue;
			}

			if (add_blob(assetHeader->Guid, static_cast<u32>(assetHeader->Type), *blob, file))
			{
				packed.insert(assetHeader->Guid);
				const auto source = sourceOf.find(assetHeader->Guid);
				WI_ASSET_LOG(Info, "Pack: {0} {1:<10} {2}", assetHeader->Guid.to_string(), asset_type_name(assetHeader->Type),
					source != sourceOf.end() ? shown_path(source->second) : std::string("(no source: the Library file alone)"));
			}
			else
			{
				ok = false;
			}
		}

		// The catalog: which path names which asset. Without it a game cannot find its assets by name.
		const std::filesystem::path catalogPath = database.get_library_root() / CATALOG_FILE_NAME;
		if (std::optional<std::vector<std::byte>> catalog = read_binary_file(catalogPath))
		{
			progress->advance(CATALOG_FILE_NAME);
			if (settings != nullptr)
			{
				// only the packed assets can be found by path
				std::optional<AssetCatalog> whole = deserialize<AssetCatalog>(*catalog);
				if (whole)
				{
					AssetCatalog kept;
					for (const AssetCatalog::Entry& entry : whole->Entries)
					{
						if (packed.contains(entry.Guid))
						{
							kept.Entries.push_back(entry);
						}
					}
					for (const AssetCatalog::Redirect& redirect : whole->Redirects)
					{
						if (packed.contains(redirect.To))
						{
							kept.Redirects.push_back(redirect);
						}
					}
					catalog = serialize(kept);
				}
				else
				{
					WI_ASSET_LOG(Error, "The asset catalog ({0}) is damaged", catalogPath.string());
					ok = false;
				}
			}
			ok = add_blob(CATALOG_GUID, 0, *catalog, catalogPath) && ok;
			log_extra(CATALOG_GUID, "asset catalog (path to guid)");
		}
		else
		{
			WI_ASSET_LOG(Error, "There is no asset catalog ({0}), the import did not finish?", catalogPath.string());
			ok = false;
		}

		// Which scene a game starts with when it is not told one.
		if (settings != nullptr && settings->DefaultScene.valid())
		{
			BuildInfo info;
			info.DefaultScene = settings->DefaultScene;
			ok = add_blob(BUILD_INFO_GUID, 0, serialize(info), "build info") && ok;
			log_extra(BUILD_INFO_GUID, "build info (default scene)");
		}

		// The project's settings files, each packed under its own blob guid if the editor has saved it (an empty path packs
		// none, so a check does not pick up the project's files). A file that exists but cannot be loaded fails the pack.
		const SettingsBlob settingsBlobs[] = {
			{ project_blob_guid(ProjectBlob::PhysicsSettings), "physics settings", &physicsSettingsFile, settings_loader<PhysicsLayers>(load_physics_layers) },
			{ project_blob_guid(ProjectBlob::InputSettings), "input settings", &inputSettingsFile, settings_loader<InputProjectSettings>(load_input_settings) },
			{ project_blob_guid(ProjectBlob::NavSettings), "navigation settings", &navSettingsFile, settings_loader<NavSettings>(load_nav_settings) },
			{ project_blob_guid(ProjectBlob::AudioSettings), "audio settings", &audioSettingsFile, settings_loader<AudioProjectSettings>(load_audio_settings) },
			{ project_blob_guid(ProjectBlob::ProjectSettings), "project settings", &projectSettingsFile,
				settings_loader<ProjectSettings>([](const std::filesystem::path& file, ProjectSettings& out, std::string& why)
				{
					std::optional<ProjectSettings> loaded = load_project_settings(file, why);
					if (loaded)
					{
						out = std::move(*loaded);
					}
					return loaded.has_value();
				}) },
		};

		for (const SettingsBlob& blob : settingsBlobs)
		{
			std::error_code exists;
			if (blob.File->empty() || !std::filesystem::exists(*blob.File, exists))
			{
				continue;
			}

			std::vector<std::byte> bytes;
			std::string why;
			if (blob.Load(*blob.File, bytes, why))
			{
				ok = add_blob(blob.Guid, 0, bytes, blob.Label) && ok;
				log_extra(blob.Guid, shown_path(blob.File->generic_string()));
			}
			else
			{
				WI_ASSET_LOG(Error, "Packing failed: {0}: {1}", blob.File->string(), why);
				ok = false;
			}
		}

		header.EntryCount = static_cast<u32>(index.size());
		header.IndexOffset = offset;
		out.write(reinterpret_cast<const char*>(index.data()), static_cast<std::streamsize>(index.size() * sizeof(PakEntry)));

		out.seekp(0);
		out.write(reinterpret_cast<const char*>(&header), sizeof(header));
		out.close();

		if (!out || !ok)
		{
			std::filesystem::remove(temp, error);
			WI_ASSET_LOG(Error, "Packing failed, {0} was not written", target.string());
			progress->finish(false);
			return false;
		}

		std::filesystem::rename(temp, target, error);
		if (error)
		{
			WI_ASSET_LOG(Error, "Cannot move the pack to {0}", target.string());
			progress->finish(false);
			return false;
		}

		WI_ASSET_LOG(Info, "Packed {0} assets into {1} ({2} bytes, {3} before compression)", packed.size(), target.string(), offset + index.size() * sizeof(PakEntry), totalRaw);
		if (settings != nullptr)
		{
			WI_ASSET_LOG(Info, "Build settings: {0} scenes chosen, {1} assets left out", settings->Scenes.size(), excluded);
		}
		progress->finish(true);
		return true;
	}
}
