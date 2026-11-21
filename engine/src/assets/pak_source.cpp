#include <pch.h>
#include "pak_source.h"

#include <lz4.h>
#include <zstd.h>

#include "asset_catalog.h"

#include <assets/asset_log.h>

namespace wiwa
{
	// A decompressed blob is never bigger than this; keeps a corrupt index from asking for gigabytes.
	constexpr u64 MAX_ASSET_SIZE = 1ull << 30;

	PakSource::PakSource(const std::filesystem::path& file) : m_File(file, std::ios::binary | std::ios::ate)
	{
		if (!m_File)
		{
			WI_ASSET_LOG(Error, "Cannot open pack {0}", file.string());
			return;
		}

		m_FileSize = static_cast<u64>(m_File.tellg());
		m_File.seekg(0);

		PakHeader header;
		if (m_FileSize < sizeof(PakHeader) || !m_File.read(reinterpret_cast<char*>(&header), sizeof(header)))
		{
			WI_ASSET_LOG(Error, "Pack {0} is truncated", file.string());
			return;
		}

		if (header.Magic != PAK_MAGIC || header.Version < 1 || header.Version > PAK_VERSION)
		{
			WI_ASSET_LOG(Error, "Pack {0} has a bad header or an unsupported version", file.string());
			return;
		}

		const u64 indexSize = static_cast<u64>(header.EntryCount) * sizeof(PakEntry);
		if (header.IndexOffset < sizeof(PakHeader) || header.IndexOffset > m_FileSize || indexSize != m_FileSize - header.IndexOffset)
		{
			WI_ASSET_LOG(Error, "Pack {0} has a damaged index", file.string());
			return;
		}

		std::vector<PakEntry> entries(header.EntryCount);
		m_File.seekg(static_cast<std::streamoff>(header.IndexOffset));
		if (!entries.empty() && !m_File.read(reinterpret_cast<char*>(entries.data()), static_cast<std::streamsize>(indexSize)))
		{
			WI_ASSET_LOG(Error, "Pack {0} index cannot be read", file.string());
			return;
		}

		for (const PakEntry& entry : entries)
		{
			const bool inside = entry.Offset >= sizeof(PakHeader) && entry.Offset <= header.IndexOffset
				&& entry.CompressedSize <= header.IndexOffset - entry.Offset;
			if (!inside || entry.UncompressedSize > MAX_ASSET_SIZE || !entry.Guid.valid())
			{
				WI_ASSET_LOG(Error, "Pack {0} has an invalid entry {1}", file.string(), entry.Guid.to_string());
				m_Index.clear();
				return;
			}
			m_Index[entry.Guid] = entry;
		}

		m_Valid = true;
	}

	std::optional<std::vector<std::byte>> PakSource::read_catalog()
	{
		return read(CATALOG_GUID);
	}

	std::optional<std::vector<std::byte>> PakSource::read(AssetGuid guid)
	{
		if (!m_Valid)
		{
			return std::nullopt;
		}

		auto found = m_Index.find(guid);
		if (found == m_Index.end())
		{
			return std::nullopt;
		}

		const PakEntry& entry = found->second;

		const PakCodec codec = static_cast<PakCodec>(entry.Codec);
		if (codec != PakCodec::Lz4 && codec != PakCodec::Zstd && codec != PakCodec::Stored)
		{
			WI_ASSET_LOG(Error, "Asset {0} in the pack uses an unknown codec", guid.to_string());
			return std::nullopt;
		}
		if (entry.UncompressedSize > MAX_ASSET_SIZE || (codec == PakCodec::Stored && entry.CompressedSize != entry.UncompressedSize))
		{
			WI_ASSET_LOG(Error, "Asset {0} in the pack is corrupt", guid.to_string());
			return std::nullopt;
		}

		std::vector<std::byte> bytes(static_cast<size_t>(entry.UncompressedSize));
		std::vector<char> compressed;
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_File.clear();
			m_File.seekg(static_cast<std::streamoff>(entry.Offset));
			// a stored blob is read straight into the result: no second buffer
			char* target = nullptr;
			size_t count = static_cast<size_t>(entry.CompressedSize);
			if (codec == PakCodec::Stored)
			{
				target = reinterpret_cast<char*>(bytes.data());
			}
			else
			{
				compressed.resize(count);
				target = compressed.data();
			}
			if (count != 0 && !m_File.read(target, static_cast<std::streamsize>(count)))
			{
				WI_ASSET_LOG(Error, "Cannot read asset {0} from the pack", guid.to_string());
				return std::nullopt;
			}
		}

		bool decoded = true;
		if (codec == PakCodec::Lz4)
		{
			const int result = LZ4_decompress_safe(compressed.data(), reinterpret_cast<char*>(bytes.data()),
				static_cast<int>(compressed.size()), static_cast<int>(bytes.size()));
			decoded = result >= 0 && static_cast<u64>(result) == entry.UncompressedSize;
		}
		else if (codec == PakCodec::Zstd)
		{
			// one decoder state per loading thread: creating it per blob costs more than small blobs take to decode
			thread_local ZSTD_DCtx* context = ZSTD_createDCtx();
			const size_t result = context != nullptr ? ZSTD_decompressDCtx(context, bytes.data(), bytes.size(), compressed.data(), compressed.size()) : 0;
			decoded = context != nullptr && !ZSTD_isError(result) && result == entry.UncompressedSize;
		}

		if (!decoded || hash64(bytes.data(), bytes.size()) != entry.Hash)
		{
			WI_ASSET_LOG(Error, "Asset {0} in the pack is corrupt", guid.to_string());
			return std::nullopt;
		}

		return bytes;
	}
}
