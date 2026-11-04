#include <editor_common.h>
#include "packager.h"

#include <lz4.h>
#include <lz4hc.h>

#include <assets/asset_catalog.h>
#include <assets/build_info.h>
#include <assets/pak_format.h>
#include <input/input_project_settings.h>
#include <physics/physics_project_settings.h>
#include <assets/wasset_io.h>

#include <unordered_set>

#include "file_utils.h"

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	std::unordered_set<AssetGuid> select_pack_set(const std::vector<PackCandidate>& candidates, const BuildSettings& settings, std::vector<AssetGuid>& missing)
	{
		missing.clear();

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
			for (AssetGuid guid : *roots)
			{
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
			pending.insert(pending.end(), found->second->Dependencies.begin(), found->second->Dependencies.end());
		}
		return selected;
	}

	bool Packager::pack(AssetDatabase& database, const std::filesystem::path& outDirectory, JobProgress* progress, const BuildSettings* settings, const std::filesystem::path& physicsSettingsFile, const std::filesystem::path& inputSettingsFile, const std::filesystem::path& projectSettingsFile)
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
			}
			const std::unordered_set<AssetGuid> selected = select_pack_set(candidates, effective, missing);
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

		std::vector<PakEntry> index;
		std::unordered_set<AssetGuid> packed; // the assets (not the catalog or the build info) that went in
		u64 offset = sizeof(PakHeader);
		u64 totalRaw = 0;
		bool ok = true;

		// Compresses one blob into the pack and notes it in the index.
		auto add_blob = [&](AssetGuid guid, u32 type, const std::vector<std::byte>& blob, const std::filesystem::path& what) -> bool
		{
			if (blob.size() > static_cast<size_t>(LZ4_MAX_INPUT_SIZE))
			{
				WI_ASSET_LOG(Error, "{0} is too big to pack", what.string());
				return false;
			}

			std::vector<char> compressed(static_cast<size_t>(LZ4_compressBound(static_cast<int>(blob.size()))));
			const int compressedSize = LZ4_compress_HC(reinterpret_cast<const char*>(blob.data()), compressed.data(),
				static_cast<int>(blob.size()), static_cast<int>(compressed.size()), LZ4HC_CLEVEL_DEFAULT);
			if (compressedSize <= 0)
			{
				WI_ASSET_LOG(Error, "Compression of {0} failed", what.string());
				return false;
			}

			out.write(compressed.data(), compressedSize);

			PakEntry entry;
			entry.Guid = guid;
			entry.Type = type;
			entry.Offset = offset;
			entry.CompressedSize = static_cast<u64>(compressedSize);
			entry.UncompressedSize = blob.size();
			entry.Hash = hash64(blob.data(), blob.size());
			index.push_back(entry);

			offset += entry.CompressedSize;
			totalRaw += entry.UncompressedSize;
			return true;
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
					catalog = serialize(kept);
				}
				else
				{
					WI_ASSET_LOG(Error, "The asset catalog ({0}) is damaged", catalogPath.string());
					ok = false;
				}
			}
			ok = add_blob(CATALOG_GUID, 0, *catalog, catalogPath) && ok;
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
		}

		// The project's physics layers, if the editor has saved any.
		{
			std::error_code exists;
			if (std::filesystem::exists(physicsSettingsFile, exists))
			{
				PhysicsLayers layers;
				std::string layersError;
				if (load_physics_layers(physicsSettingsFile, layers, layersError))
				{
					ok = add_blob(PHYSICS_SETTINGS_GUID, 0, serialize(layers), "physics settings") && ok;
				}
				else
				{
					WI_ASSET_LOG(Error, "Packing failed: {0}: {1}", physicsSettingsFile.string(), layersError);
					ok = false;
				}
			}
		}

		// The project's input settings (the default map), if the editor has saved any.
		{
			std::error_code exists;
			if (std::filesystem::exists(inputSettingsFile, exists))
			{
				InputProjectSettings inputSettings;
				std::string inputError;
				if (load_input_settings(inputSettingsFile, inputSettings, inputError))
				{
					ok = add_blob(INPUT_SETTINGS_GUID, 0, serialize(inputSettings), "input settings") && ok;
				}
				else
				{
					WI_ASSET_LOG(Error, "Packing failed: {0}: {1}", inputSettingsFile.string(), inputError);
					ok = false;
				}
			}
		}

		// The project's name, version and launch settings, if the editor has saved any.
		{
			std::error_code exists;
			if (std::filesystem::exists(projectSettingsFile, exists))
			{
				std::string projectError;
				if (const std::optional<ProjectSettings> project = load_project_settings(projectSettingsFile, projectError))
				{
					ok = add_blob(PROJECT_SETTINGS_GUID, 0, serialize(*project), "project settings") && ok;
				}
				else
				{
					WI_ASSET_LOG(Error, "Packing failed: {0}: {1}", projectSettingsFile.string(), projectError);
					ok = false;
				}
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
