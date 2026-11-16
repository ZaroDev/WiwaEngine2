#include <pch.h>
#include "nav_data.h"
#include "nav_data_impl.h"

#include <cstring>

#include <DetourAlloc.h>

namespace wiwa
{
	NavData::NavData() : m_Impl(CreateScope<Impl>()) {}

	NavData::~NavData() = default;

	Scope<NavData> NavData::create(const NavMeshData& mesh, std::string& error)
	{
		if (!mesh.valid())
		{
			error = "the navmesh is not valid";
			return nullptr;
		}

		Scope<NavData> data(new NavData());
		data->m_Source = mesh;
		data->m_Impl->Mesh = dtAllocNavMesh();
		if (data->m_Impl->Mesh == nullptr)
		{
			error = "out of memory";
			return nullptr;
		}

		dtNavMeshParams params{};
		params.orig[0] = mesh.Origin.x;
		params.orig[1] = mesh.Origin.y;
		params.orig[2] = mesh.Origin.z;
		params.tileWidth = mesh.TileWidth;
		params.tileHeight = mesh.TileHeight;
		params.maxTiles = mesh.MaxTiles;
		params.maxPolys = mesh.MaxPolys;
		if (dtStatusFailed(data->m_Impl->Mesh->init(&params)))
		{
			error = "Detour refuses the navmesh parameters";
			return nullptr;
		}

		for (const NavTileBlob& tile : mesh.Tiles)
		{
			const int size = static_cast<int>(tile.Data.size());
			unsigned char* memory = static_cast<unsigned char*>(dtAlloc(static_cast<size_t>(size), DT_ALLOC_PERM));
			if (memory == nullptr)
			{
				error = "out of memory";
				return nullptr;
			}
			std::memcpy(memory, tile.Data.data(), tile.Data.size());
			if (dtStatusFailed(data->m_Impl->Mesh->addTile(memory, size, DT_TILE_FREE_DATA, 0, nullptr)))
			{
				dtFree(memory);
				error = "Detour refuses a tile of the navmesh";
				return nullptr;
			}
		}
		return data;
	}
}
