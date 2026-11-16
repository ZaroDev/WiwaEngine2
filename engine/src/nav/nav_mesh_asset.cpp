#include <pch.h>
#include "nav_mesh_asset.h"

#include <cstring>
#include <optional>

#include <assets/wasset_io.h>

namespace wiwa
{
	namespace
	{
		constexpr u32 NAV_MESH_MAGIC = 0x56414E57; // "WNAV"
		constexpr u32 NAV_MESH_VERSION = 1;
	}

	bool NavMeshData::valid() const
	{
		if (Version != NAV_MESH_VERSION || MaxTiles <= 0 || MaxPolys <= 0 || Tiles.empty() || Tiles.size() > static_cast<size_t>(MaxTiles))
		{
			return false;
		}
		if (!(TileWidth >= 0.0f) || !(TileHeight >= 0.0f))
		{
			return false;
		}
		for (const NavTileBlob& tile : Tiles)
		{
			if (tile.Data.empty())
			{
				return false;
			}
		}
		return true;
	}

	void archive(BinaryArchive& ar, NavMeshData& v)
	{
		u32 magic = NAV_MESH_MAGIC;
		ar.value(magic);
		u32 version = v.Version;
		ar.value(version);
		if (ar.reading() && ar.ok() && (magic != NAV_MESH_MAGIC || version != NAV_MESH_VERSION))
		{
			ar.fail();
			return;
		}
		v.Version = version;

		ar.value(v.Build);
		ar.value(v.InputHash);
		ar.value(v.Origin);
		ar.value(v.TileWidth);
		ar.value(v.TileHeight);
		ar.value(v.MaxTiles);
		ar.value(v.MaxPolys);
		ar.list(v.Tiles, [&ar](NavTileBlob& tile) { ar.pod_vector(tile.Data); });

		if (ar.reading() && ar.ok() && (ar.remaining() != 0 || !v.valid()))
		{
			ar.fail();
		}
	}

	std::vector<std::byte> nav_mesh_to_bytes(const NavMeshData& data)
	{
		return serialize(data);
	}

	bool nav_mesh_from_bytes(std::span<const std::byte> bytes, NavMeshData& data, std::string& error)
	{
		if (bytes.size() < 2 * sizeof(u32))
		{
			error = "the file is too short to be a navmesh";
			return false;
		}
		u32 magic = 0;
		std::memcpy(&magic, bytes.data(), sizeof(u32));
		if (magic != NAV_MESH_MAGIC)
		{
			error = "not a .wnav file (wrong magic)";
			return false;
		}
		std::optional<NavMeshData> read = deserialize<NavMeshData>(bytes);
		if (!read)
		{
			error = "the navmesh is truncated, corrupt, or of another version";
			return false;
		}
		data = std::move(*read);
		return true;
	}
}
