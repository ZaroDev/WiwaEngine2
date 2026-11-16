#pragma once

#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include <core/common.h>
#include "nav_types.h"

namespace wiwa
{
	// One Detour tile, as dtCreateNavMeshData wrote it.
	struct NavTileBlob
	{
		std::vector<std::byte> Data{};

		bool operator==(const NavTileBlob&) const = default;
	};

	// A baked navigation mesh (.wnav, AssetType::NavMesh): the Detour tiles and the numbers the navmesh is created with, plus what it was built from
	// (the settings, and a hash of the input geometry so the editor can tell a stale bake). CPU-side only; the Detour objects are made by NavData.
	struct NavMeshData
	{
		u32 Version = 1;
		NavBuildSettings Build{};
		u64 InputHash = 0;
		glm::vec3 Origin{ 0.0f };
		float TileWidth = 0.0f;   // world size of a tile
		float TileHeight = 0.0f;
		i32 MaxTiles = 0;
		i32 MaxPolys = 0;         // per tile
		std::vector<NavTileBlob> Tiles{};

		bool valid() const;
		bool operator==(const NavMeshData&) const = default;
	};

	// The .wnav file is the archived NavMeshData (magic, version, values). False, with why in error, for a wrong magic or version, truncated or
	// trailing bytes, or a mesh that is not valid().
	std::vector<std::byte> nav_mesh_to_bytes(const NavMeshData& data);
	bool nav_mesh_from_bytes(std::span<const std::byte> bytes, NavMeshData& data, std::string& error);
}
