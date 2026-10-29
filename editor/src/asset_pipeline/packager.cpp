#include <editor_common.h>
#include "packager.h"

#include <lz4.h>
#include <lz4hc.h>

#include <assets/asset_catalog.h>
#include <assets/pak_format.h>
#include <assets/wasset_io.h>

#include "file_utils.h"

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	bool Packager::pack(AssetDatabase& database, const std::filesystem::path& outDirectory, JobProgress* progress)
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

			ok = add_blob(assetHeader->Guid, static_cast<u32>(assetHeader->Type), *blob, file) && ok;
		}

		// The catalog: which path names which asset. Without it a game cannot find its assets by name.
		const std::filesystem::path catalogPath = database.get_library_root() / CATALOG_FILE_NAME;
		if (std::optional<std::vector<std::byte>> catalog = read_binary_file(catalogPath))
		{
			progress->advance(CATALOG_FILE_NAME);
			ok = add_blob(CATALOG_GUID, 0, *catalog, catalogPath) && ok;
		}
		else
		{
			WI_ASSET_LOG(Error, "There is no asset catalog ({0}), the import did not finish?", catalogPath.string());
			ok = false;
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

		WI_ASSET_LOG(Info, "Packed {0} assets into {1} ({2} bytes, {3} before compression)", index.size() - 1, target.string(), offset + index.size() * sizeof(PakEntry), totalRaw);
		progress->finish(true);
		return true;
	}
}
