#pragma once

#include <array>
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
		Custom3 = 7
	};

	constexpr u32 NAV_AREA_COUNT = 8;

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

	// The cost of walking one metre of each area (at least 1), kept in the project settings.
	struct NavSettings
	{
		std::array<float, NAV_AREA_COUNT> AreaCost{ 1, 1, 1, 1, 1, 1, 1, 1 };

		bool operator==(const NavSettings&) const = default;
	};
}
