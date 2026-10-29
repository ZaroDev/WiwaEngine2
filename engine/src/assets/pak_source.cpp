#include <pch.h>
#include "pak_source.h"

#include <lz4.h>

#define WI_ASSET_LOG(level, ...) WI_LOG(level, "Assets", __VA_ARGS__)

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

		if (header.Magic != PAK_MAGIC || header.Version != PAK_VERSION)
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

		std::vector<char> compressed(static_cast<size_t>(entry.CompressedSize));
		{
			std::lock_guard<std::mutex> lock(m_Mutex);
			m_File.clear();
			m_File.seekg(static_cast<std::streamoff>(entry.Offset));
			if (!compressed.empty() && !m_File.read(compressed.data(), static_cast<std::streamsize>(compressed.size())))
			{
				WI_ASSET_LOG(Error, "Cannot read asset {0} from the pack", guid.to_string());
				return std::nullopt;
			}
		}

		std::vector<std::byte> bytes(static_cast<size_t>(entry.UncompressedSize));
		const int decoded = LZ4_decompress_safe(compressed.data(), reinterpret_cast<char*>(bytes.data()),
			static_cast<int>(compressed.size()), static_cast<int>(bytes.size()));

		if (decoded < 0 || static_cast<u64>(decoded) != entry.UncompressedSize || hash64(bytes.data(), bytes.size()) != entry.Hash)
		{
			WI_ASSET_LOG(Error, "Asset {0} in the pack is corrupt", guid.to_string());
			return std::nullopt;
		}

		return bytes;
	}
}
