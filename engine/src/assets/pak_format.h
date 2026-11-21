#pragma once

#include <math/math.h>
#include "asset_guid.h"

// assets.wpak: PakHeader, then the .wasset blobs (each compressed as its PakEntry::Codec says), then the index (PakEntry * EntryCount) at IndexOffset.
namespace wiwa
{
	constexpr u32 PAK_MAGIC = 0x4B415057; // "WPAK"
	constexpr u32 PAK_VERSION = 2; // 2: entries name their codec (Zstd, or stored when compression does not pay). 1: every blob LZ4

	struct PakHeader
	{
		u32 Magic = PAK_MAGIC;
		u32 Version = PAK_VERSION;
		u32 EntryCount = 0;
		u32 Reserved = 0;
		u64 IndexOffset = 0;
	};

	// How a blob is stored in the pack. Written to files: append only.
	enum class PakCodec : u32
	{
		Lz4 = 0,
		Zstd = 1,
		Stored = 2, // as is: CompressedSize == UncompressedSize
	};

	struct PakEntry
	{
		AssetGuid Guid{};
		u32 Type = 0;
		u32 Codec = static_cast<u32>(PakCodec::Lz4);
		u64 Offset = 0;
		u64 CompressedSize = 0;
		u64 UncompressedSize = 0;
		u64 Hash = 0; // hash64 of the uncompressed bytes
	};

	// FNV-1a
	inline u64 hash64(const void* data, size_t size)
	{
		const u8* bytes = static_cast<const u8*>(data);
		u64 hash = 0xcbf29ce484222325ull;
		for (size_t i = 0; i < size; i++)
		{
			hash ^= bytes[i];
			hash *= 0x100000001b3ull;
		}
		return hash;
	}
}
