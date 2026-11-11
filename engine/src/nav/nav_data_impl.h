#pragma once

#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>

#include "nav_data.h"

// Detour's side of NavData, for the files inside engine/src/nav only.
namespace wiwa
{
	struct NavData::Impl
	{
		dtNavMesh* Mesh = nullptr;

		~Impl()
		{
			dtFreeNavMesh(Mesh);
		}
	};

	inline void fill_detour_filter(dtQueryFilter& out, const NavFilter& filter)
	{
		out.setIncludeFlags(static_cast<unsigned short>(filter.AreaMask & 0xFFFFu));
		out.setExcludeFlags(0);
		for (u32 i = 0; i < NAV_AREA_COUNT; i++)
		{
			out.setAreaCost(static_cast<int>(i), filter.AreaCost[i]);
		}
	}
}
