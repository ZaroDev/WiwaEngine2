#pragma once

#include <array>
#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include <nav/nav_mesh_asset.h>
#include <nav/nav_types.h>

// Bakes a navigation mesh with Recast (the only place Recast is used; the engine only loads and queries what this writes). The input is plain
// data, so a bake can run on a worker thread from a snapshot of the scene (nav_geometry.h gathers it), and a check can bake a mesh it built itself.
namespace wiwa
{
	struct NavBakeInput
	{
		std::vector<glm::vec3> Vertices{};
		std::vector<u32> Indices{};         // three per triangle
		std::vector<u8> TriangleAreas{};    // a NavArea per triangle; empty: every triangle is Walkable (a triangle too steep to walk on is not, whatever it says)
		glm::vec3 BoundsMin{ 0.0f };        // the box that is baked; geometry outside it is ignored
		glm::vec3 BoundsMax{ 0.0f };
		NavBuildSettings Build{};
	};

	struct NavBakeResult
	{
		bool Ok = false;
		std::string Error{};                // why not, when !Ok
		NavMeshData Mesh{};
		u32 PolyCount = 0;
		u32 VertCount = 0;
		u32 TileCount = 0;
		std::array<u32, NAV_AREA_COUNT> AreaPolyCount{}; // polygons per area id
		float Seconds = 0.0f;
	};

	// A hash (64 bit FNV-1a) of everything that decides the result: the geometry, the areas, the bounds and the settings.
	u64 nav_input_hash(const NavBakeInput& input);

	// Why the input cannot be baked ("" when it can): no triangles, an index out of range, a vertex that is not finite, an empty or inverted box,
	// a setting out of its range, or a grid of more than NAV_BAKE_MAX_TILES tiles.
	constexpr u32 NAV_BAKE_MAX_TILES = 4096;
	std::string nav_validate_input(const NavBakeInput& input);

	// The standard Recast tile pipeline. Checks `cancel` between tiles and stages (a cancelled bake is !Ok with Error "cancelled"); `progress` gets a
	// fraction 0..1 and a phase name. The result is the same for the same input and settings, byte for byte (nav_mesh_to_bytes).
	NavBakeResult bake_navmesh(const NavBakeInput& input, const std::atomic<bool>* cancel = nullptr, const std::function<void(float, const char*)>& progress = {});

	// Writes the .wnav file through a temp file and a rename, so a reader never sees half a file. False, and why in error, if it could not.
	bool write_nav_file(const std::filesystem::path& path, const NavMeshData& mesh, std::string& error);
}
