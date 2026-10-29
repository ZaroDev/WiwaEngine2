#include <editor_common.h>
#include "packager.h"

#include <lz4.h>
#include <lz4hc.h>

#include <assets/pak_format.h>
#include <assets/wasset_io.h>

#include "file_utils.h"

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

namespace wiwa
{
	bool Packager::pack(AssetDatabase& database, const std::filesystem::path& outDirectory)
	{
		// Library/ is brought up to date first, so the pack never holds stale files.
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

		std::filesystem::create_directories(outDirectory, error);
		const std::filesystem::path target = outDirectory / "assets.wpak";
		std::filesystem::path temp = target;
		temp += ".tmp";

		std::ofstream out(temp, std::ios::binary | std::ios::trunc);
		if (!out)
		{
			WI_ASSET_LOG(Error, "Cannot write {0}", temp.string());
			return false;
		}

		PakHeader header;
		out.write(reinterpret_cast<const char*>(&header), sizeof(header)); // patched at the end

		std::vector<PakEntry> index;
		u64 offset = sizeof(PakHeader);
		u64 totalRaw = 0;
		bool ok = true;

		for (const std::filesystem::path& file : files)
		{
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

			if (blob->size() > static_cast<size_t>(LZ4_MAX_INPUT_SIZE))
			{
				WI_ASSET_LOG(Error, "{0} is too big to pack", file.string());
				ok = false;
				continue;
			}

			std::vector<char> compressed(static_cast<size_t>(LZ4_compressBound(static_cast<int>(blob->size()))));
			const int compressedSize = LZ4_compress_HC(reinterpret_cast<const char*>(blob->data()), compressed.data(),
				static_cast<int>(blob->size()), static_cast<int>(compressed.size()), LZ4HC_CLEVEL_DEFAULT);
			if (compressedSize <= 0)
			{
				WI_ASSET_LOG(Error, "Compression of {0} failed", file.string());
				ok = false;
				continue;
			}

			out.write(compressed.data(), compressedSize);

			PakEntry entry;
			entry.Guid = assetHeader->Guid;
			entry.Type = static_cast<u32>(assetHeader->Type);
			entry.Offset = offset;
			entry.CompressedSize = static_cast<u64>(compressedSize);
			entry.UncompressedSize = blob->size();
			entry.Hash = hash64(blob->data(), blob->size());
			index.push_back(entry);

			offset += entry.CompressedSize;
			totalRaw += entry.UncompressedSize;
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
			return false;
		}

		std::filesystem::rename(temp, target, error);
		if (error)
		{
			WI_ASSET_LOG(Error, "Cannot move the pack to {0}", target.string());
			return false;
		}

		WI_ASSET_LOG(Info, "Packed {0} assets into {1} ({2} bytes, {3} before compression)", index.size(), target.string(), offset + index.size() * sizeof(PakEntry), totalRaw);
		return true;
	}
}
