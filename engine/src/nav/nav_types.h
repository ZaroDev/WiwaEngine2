#pragma once

#include <array>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include <core/common.h>

namespace wiwa
{
	// The kind of ground a navmesh polygon is. The id is what Recast and Detour store per polygon (0 is Recast's "not walkable"); the polygon flag
	// of an area is 1 << id, so an agent's area mask and the cost table are both indexed by it. Written to .wnav files: append, never reorder.
	enum class NavArea : u8
	{
		NotWalkable = 0,
		Walkable = 1,
		Mud = 2,
		Water = 3,
		Custom0 = 4,
		Custom1 = 5,
		Custom2 = 6,
		Custom3 = 7,
		Area8 = 8,
		Area9 = 9,
		Area10 = 10,
		Area11 = 11,
		Area12 = 12,
		Area13 = 13,
		Area14 = 14,
		Area15 = 15
	};

	// Ids 0 and 1 are fixed; 2..15 are slots a project names (an empty name is an unused slot). A polygon flag is 16 bits wide, so 16 is the most there can be.
	constexpr u32 NAV_AREA_COUNT = 16;

	constexpr u32 nav_area_flag(NavArea area) { return 1u << static_cast<u32>(area); }

	// What Recast bakes a navmesh with (agent size and the voxel grid). The defaults suit a 1.8 m humanoid; units are metres except where noted.
	struct NavBuildSettings
	{
		float AgentRadius = 0.4f;
		float AgentHeight = 1.8f;
		float AgentMaxClimb = 0.4f;
		float AgentMaxSlope = 45.0f;     // degrees
		float CellSize = 0.2f;
		float CellHeight = 0.1f;
		i32 RegionMinSize = 8;           // cells
		i32 RegionMergeSize = 20;        // cells
		float EdgeMaxLength = 12.0f;
		float EdgeMaxError = 1.3f;
		i32 VertsPerPoly = 6;
		float DetailSampleDist = 6.0f;
		float DetailSampleMaxError = 1.0f;
		i32 TileSize = 64;               // cells per tile side

		bool operator==(const NavBuildSettings&) const = default;
	};

	enum class NavPathStatus : u8 { Failed, Partial, Complete };

	struct NavPath
	{
		std::vector<glm::vec3> Corners{};
		NavPathStatus Status = NavPathStatus::Failed;
		float Length = 0.0f;
	};

	// The areas of a project: a name and the cost of walking one metre (at least 1) per id. Ids 0 (NotWalkable) and 1 (Walkable) are always there; a slot
	// 2..15 with an empty name is unused. An id never changes when its area is renamed: baked meshes and NavAreaTag store ids.
	struct NavSettings
	{
		std::array<float, NAV_AREA_COUNT> AreaCost{};
		std::array<std::string, NAV_AREA_COUNT> AreaName{};

		NavSettings()
		{
			AreaCost.fill(1.0f);
			AreaName[0] = "NotWalkable";
			AreaName[1] = "Walkable";
			AreaName[2] = "Mud";
			AreaName[3] = "Water";
		}

		bool used(u32 id) const { return id < NAV_AREA_COUNT && (id < 2 || !AreaName[id].empty()); }

		bool operator==(const NavSettings&) const = default;
	};

	// The id of the area with that name (exact match), or -1.
	inline int nav_area_id(const NavSettings& settings, const std::string& name)
	{
		for (u32 i = 0; i < NAV_AREA_COUNT; i++)
		{
			if (settings.used(i) && settings.AreaName[i] == name)
			{
				return static_cast<int>(i);
			}
		}
		return -1;
	}
}
