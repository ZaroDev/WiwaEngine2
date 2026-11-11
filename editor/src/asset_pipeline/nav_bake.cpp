#include <editor_common.h>
#include "nav_bake.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>

#include <DetourAlloc.h>
#include <DetourCommon.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <Recast.h>

#include "atomic_write.h"

namespace wiwa
{
	namespace
	{
		// Recast's log goes to the nav log.
		class BakeContext final : public rcContext
		{
		protected:
			virtual void doLog(const rcLogCategory category, const char* message, const int length) override
			{
				const std::string text(message, static_cast<size_t>(length));
				switch (category)
				{
				case RC_LOG_ERROR: WI_NAV_LOG(Error, "Recast: {0}", text); break;
				case RC_LOG_WARNING: WI_NAV_LOG(Warning, "Recast: {0}", text); break;
				default: WI_NAV_LOG(Trace, "Recast: {0}", text); break;
				}
			}
		};

		struct CompactDeleter { void operator()(rcCompactHeightfield* p) const { rcFreeCompactHeightfield(p); } };
		struct ContourDeleter { void operator()(rcContourSet* p) const { rcFreeContourSet(p); } };
		struct PolyMeshDeleter { void operator()(rcPolyMesh* p) const { rcFreePolyMesh(p); } };
		struct DetailDeleter { void operator()(rcPolyMeshDetail* p) const { rcFreePolyMeshDetail(p); } };

		u64 fnv(u64 hash, const void* data, size_t size)
		{
			const unsigned char* bytes = static_cast<const unsigned char*>(data);
			for (size_t i = 0; i < size; i++)
			{
				hash ^= bytes[i];
				hash *= 1099511628211ull;
			}
			return hash;
		}

		bool finite3(const glm::vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

		struct Grid
		{
			int Width = 0;   // cells
			int Height = 0;
			int TilesX = 0;
			int TilesZ = 0;
			float TileWorld = 0.0f; // size of a tile in metres
		};

		Grid make_grid(const NavBakeInput& input)
		{
			Grid grid;
			rcCalcGridSize(&input.BoundsMin.x, &input.BoundsMax.x, input.Build.CellSize, &grid.Width, &grid.Height);
			grid.TilesX = (grid.Width + input.Build.TileSize - 1) / input.Build.TileSize;
			grid.TilesZ = (grid.Height + input.Build.TileSize - 1) / input.Build.TileSize;
			grid.TileWorld = static_cast<float>(input.Build.TileSize) * input.Build.CellSize;
			return grid;
		}

		struct TileResult
		{
			bool Ok = true;            // false: an error, with its reason in Error
			std::string Error{};
			std::vector<std::byte> Data{}; // empty: nothing walkable in the tile
			u32 PolyCount = 0;
			u32 VertCount = 0;
			std::array<u32, NAV_AREA_COUNT> AreaPolyCount{};
		};

		bool cancelled(const std::atomic<bool>* cancel) { return cancel != nullptr && cancel->load(); }

		TileResult build_tile(BakeContext& context, const NavBakeInput& input, const std::vector<glm::vec4>& triangleBounds, const Grid& grid, int tileX, int tileZ, i32 maxPolys, const std::atomic<bool>* cancel)
		{
			TileResult result;
			const NavBuildSettings& s = input.Build;

			rcConfig config{};
			config.cs = s.CellSize;
			config.ch = s.CellHeight;
			config.walkableSlopeAngle = s.AgentMaxSlope;
			config.walkableHeight = static_cast<int>(std::ceil(s.AgentHeight / s.CellHeight));
			config.walkableClimb = static_cast<int>(std::floor(s.AgentMaxClimb / s.CellHeight));
			config.walkableRadius = static_cast<int>(std::ceil(s.AgentRadius / s.CellSize));
			config.maxEdgeLen = static_cast<int>(s.EdgeMaxLength / s.CellSize);
			config.maxSimplificationError = s.EdgeMaxError;
			config.minRegionArea = s.RegionMinSize * s.RegionMinSize;
			config.mergeRegionArea = s.RegionMergeSize * s.RegionMergeSize;
			config.maxVertsPerPoly = s.VertsPerPoly;
			config.tileSize = s.TileSize;
			config.borderSize = config.walkableRadius + 3;
			config.width = config.tileSize + config.borderSize * 2;
			config.height = config.tileSize + config.borderSize * 2;
			config.detailSampleDist = s.DetailSampleDist < 0.9f ? 0.0f : s.CellSize * s.DetailSampleDist;
			config.detailSampleMaxError = s.CellHeight * s.DetailSampleMaxError;

			const float border = static_cast<float>(config.borderSize) * s.CellSize;
			config.bmin[0] = input.BoundsMin.x + static_cast<float>(tileX) * grid.TileWorld - border;
			config.bmin[1] = input.BoundsMin.y;
			config.bmin[2] = input.BoundsMin.z + static_cast<float>(tileZ) * grid.TileWorld - border;
			config.bmax[0] = input.BoundsMin.x + static_cast<float>(tileX + 1) * grid.TileWorld + border;
			config.bmax[1] = input.BoundsMax.y;
			config.bmax[2] = input.BoundsMin.z + static_cast<float>(tileZ + 1) * grid.TileWorld + border;

			// The triangles that touch the tile (with its border).
			std::vector<int> triangles;
			for (size_t t = 0; t < triangleBounds.size(); t++)
			{
				const glm::vec4& b = triangleBounds[t]; // minX, minZ, maxX, maxZ
				if (b.z >= config.bmin[0] && b.x <= config.bmax[0] && b.w >= config.bmin[2] && b.y <= config.bmax[2])
				{
					triangles.push_back(static_cast<int>(t));
				}
			}
			if (triangles.empty())
			{
				return result;
			}

			std::vector<int> tileIndices;
			tileIndices.reserve(triangles.size() * 3);
			for (const int t : triangles)
			{
				tileIndices.push_back(static_cast<int>(input.Indices[static_cast<size_t>(t) * 3 + 0]));
				tileIndices.push_back(static_cast<int>(input.Indices[static_cast<size_t>(t) * 3 + 1]));
				tileIndices.push_back(static_cast<int>(input.Indices[static_cast<size_t>(t) * 3 + 2]));
			}
			const int triangleCount = static_cast<int>(triangles.size());
			const float* vertices = &input.Vertices.front().x;
			const int vertexCount = static_cast<int>(input.Vertices.size());

			// What is walkable by slope becomes the area the triangle is tagged with.
			std::vector<unsigned char> areas(static_cast<size_t>(triangleCount), 0);
			rcMarkWalkableTriangles(&context, config.walkableSlopeAngle, vertices, vertexCount, tileIndices.data(), triangleCount, areas.data());
			for (int i = 0; i < triangleCount; i++)
			{
				if (areas[static_cast<size_t>(i)] != RC_WALKABLE_AREA)
				{
					areas[static_cast<size_t>(i)] = RC_NULL_AREA;
					continue;
				}
				const u8 tag = input.TriangleAreas.empty() ? static_cast<u8>(NavArea::Walkable) : input.TriangleAreas[static_cast<size_t>(triangles[static_cast<size_t>(i)])];
				areas[static_cast<size_t>(i)] = tag < NAV_AREA_COUNT ? tag : static_cast<u8>(NavArea::Walkable);
			}

			rcHeightfield heightfield;
			if (!rcCreateHeightfield(&context, heightfield, config.width, config.height, config.bmin, config.bmax, config.cs, config.ch))
			{
				result.Ok = false;
				result.Error = "cannot create the heightfield (out of memory)";
				return result;
			}
			if (!rcRasterizeTriangles(&context, vertices, vertexCount, tileIndices.data(), areas.data(), triangleCount, heightfield, config.walkableClimb))
			{
				result.Ok = false;
				result.Error = "cannot rasterize the triangles";
				return result;
			}

			rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, heightfield);
			rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, heightfield);
			rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, heightfield);
			if (cancelled(cancel))
			{
				return result;
			}

			std::unique_ptr<rcCompactHeightfield, CompactDeleter> compact(rcAllocCompactHeightfield());
			if (!compact || !rcBuildCompactHeightfield(&context, config.walkableHeight, config.walkableClimb, heightfield, *compact))
			{
				result.Ok = false;
				result.Error = "cannot build the compact heightfield";
				return result;
			}
			if (!rcErodeWalkableArea(&context, config.walkableRadius, *compact))
			{
				result.Ok = false;
				result.Error = "cannot erode the walkable area";
				return result;
			}
			if (!rcBuildDistanceField(&context, *compact) || !rcBuildRegions(&context, *compact, config.borderSize, config.minRegionArea, config.mergeRegionArea))
			{
				result.Ok = false;
				result.Error = "cannot build the regions";
				return result;
			}
			if (cancelled(cancel))
			{
				return result;
			}

			std::unique_ptr<rcContourSet, ContourDeleter> contours(rcAllocContourSet());
			if (!contours || !rcBuildContours(&context, *compact, config.maxSimplificationError, config.maxEdgeLen, *contours))
			{
				result.Ok = false;
				result.Error = "cannot build the contours";
				return result;
			}
			if (contours->nconts == 0)
			{
				return result;
			}

			std::unique_ptr<rcPolyMesh, PolyMeshDeleter> polyMesh(rcAllocPolyMesh());
			if (!polyMesh || !rcBuildPolyMesh(&context, *contours, config.maxVertsPerPoly, *polyMesh))
			{
				result.Ok = false;
				result.Error = "cannot build the polygon mesh";
				return result;
			}
			std::unique_ptr<rcPolyMeshDetail, DetailDeleter> detail(rcAllocPolyMeshDetail());
			if (!detail || !rcBuildPolyMeshDetail(&context, *polyMesh, *compact, config.detailSampleDist, config.detailSampleMaxError, *detail))
			{
				result.Ok = false;
				result.Error = "cannot build the detail mesh";
				return result;
			}
			if (polyMesh->nverts >= 0xffff)
			{
				result.Ok = false;
				result.Error = "a tile has too many vertices: use a smaller tile size";
				return result;
			}
			if (polyMesh->npolys == 0)
			{
				return result;
			}
			if (polyMesh->npolys > maxPolys)
			{
				result.Ok = false;
				result.Error = "a tile has " + std::to_string(polyMesh->npolys) + " polygons, at most " + std::to_string(maxPolys) + " fit: use a smaller tile size";
				return result;
			}

			for (int i = 0; i < polyMesh->npolys; i++)
			{
				const unsigned char area = polyMesh->areas[i];
				if (area >= NAV_AREA_COUNT)
				{
					polyMesh->areas[i] = static_cast<unsigned char>(NavArea::Walkable);
				}
				polyMesh->flags[i] = static_cast<unsigned short>(1u << polyMesh->areas[i]);
				result.AreaPolyCount[polyMesh->areas[i]]++;
			}

			dtNavMeshCreateParams params{};
			params.verts = polyMesh->verts;
			params.vertCount = polyMesh->nverts;
			params.polys = polyMesh->polys;
			params.polyAreas = polyMesh->areas;
			params.polyFlags = polyMesh->flags;
			params.polyCount = polyMesh->npolys;
			params.nvp = polyMesh->nvp;
			params.detailMeshes = detail->meshes;
			params.detailVerts = detail->verts;
			params.detailVertsCount = detail->nverts;
			params.detailTris = detail->tris;
			params.detailTriCount = detail->ntris;
			params.walkableHeight = s.AgentHeight;
			params.walkableRadius = s.AgentRadius;
			params.walkableClimb = s.AgentMaxClimb;
			params.tileX = tileX;
			params.tileY = tileZ;
			params.tileLayer = 0;
			rcVcopy(params.bmin, polyMesh->bmin);
			rcVcopy(params.bmax, polyMesh->bmax);
			params.cs = config.cs;
			params.ch = config.ch;
			params.buildBvTree = true;

			unsigned char* navData = nullptr;
			int navDataSize = 0;
			if (!dtCreateNavMeshData(&params, &navData, &navDataSize))
			{
				result.Ok = false;
				result.Error = "Detour cannot create the tile data";
				return result;
			}
			result.Data.resize(static_cast<size_t>(navDataSize));
			std::memcpy(result.Data.data(), navData, static_cast<size_t>(navDataSize));
			dtFree(navData);

			result.PolyCount = static_cast<u32>(polyMesh->npolys);
			result.VertCount = static_cast<u32>(polyMesh->nverts);
			return result;
		}
	}

	u64 nav_input_hash(const NavBakeInput& input)
	{
		u64 hash = 14695981039346656037ull;
		hash = fnv(hash, input.Vertices.data(), input.Vertices.size() * sizeof(glm::vec3));
		hash = fnv(hash, input.Indices.data(), input.Indices.size() * sizeof(u32));
		hash = fnv(hash, input.TriangleAreas.data(), input.TriangleAreas.size());
		hash = fnv(hash, &input.BoundsMin, sizeof(glm::vec3));
		hash = fnv(hash, &input.BoundsMax, sizeof(glm::vec3));
		hash = fnv(hash, &input.Build, sizeof(NavBuildSettings));
		return hash;
	}

	std::string nav_validate_input(const NavBakeInput& input)
	{
		const NavBuildSettings& s = input.Build;
		if (input.Indices.empty() || input.Vertices.empty())
		{
			return "there is no geometry to bake";
		}
		if (input.Indices.size() % 3 != 0)
		{
			return "the index count is not a multiple of three";
		}
		if (!input.TriangleAreas.empty() && input.TriangleAreas.size() != input.Indices.size() / 3)
		{
			return "there must be one area per triangle";
		}
		for (const u32 index : input.Indices)
		{
			if (index >= input.Vertices.size())
			{
				return "a triangle uses a vertex that does not exist";
			}
		}
		for (const glm::vec3& vertex : input.Vertices)
		{
			if (!finite3(vertex))
			{
				return "a vertex is not a finite number";
			}
		}
		if (!finite3(input.BoundsMin) || !finite3(input.BoundsMax) || !(input.BoundsMin.x < input.BoundsMax.x) || !(input.BoundsMin.y < input.BoundsMax.y) || !(input.BoundsMin.z < input.BoundsMax.z))
		{
			return "the volume is empty or not a finite box";
		}
		if (!(s.CellSize >= 0.01f && s.CellSize <= 10.0f) || !(s.CellHeight >= 0.01f && s.CellHeight <= 10.0f))
		{
			return "the cell size and the cell height must be between 0.01 and 10 m";
		}
		if (!(s.AgentRadius >= 0.0f) || !(s.AgentHeight > 0.0f) || !(s.AgentMaxClimb >= 0.0f) || !(s.AgentMaxSlope >= 0.0f && s.AgentMaxSlope < 90.0f))
		{
			return "the agent size or slope is out of range";
		}
		if (s.VertsPerPoly < 3 || s.VertsPerPoly > DT_VERTS_PER_POLYGON || s.TileSize < 8 || s.TileSize > 1024 || s.RegionMinSize < 0 || s.RegionMergeSize < 0 || !(s.EdgeMaxLength >= 0.0f) || !(s.EdgeMaxError > 0.0f) || !(s.DetailSampleDist >= 0.0f) || !(s.DetailSampleMaxError >= 0.0f))
		{
			return "a build setting is out of range";
		}

		const Grid grid = make_grid(input);
		if (grid.Width <= 0 || grid.Height <= 0 || static_cast<i64>(grid.TilesX) * grid.TilesZ > NAV_BAKE_MAX_TILES)
		{
			return "the volume needs more than " + std::to_string(NAV_BAKE_MAX_TILES) + " tiles: make it smaller or the cells bigger";
		}
		return {};
	}

	NavBakeResult bake_navmesh(const NavBakeInput& input, const std::atomic<bool>* cancel, const std::function<void(float, const char*)>& progress)
	{
		NavBakeResult result;
		const auto start = std::chrono::steady_clock::now();

		result.Error = nav_validate_input(input);
		if (!result.Error.empty())
		{
			return result;
		}

		const Grid grid = make_grid(input);
		const i32 tileCount = grid.TilesX * grid.TilesZ;
		i32 tileBits = 0;
		while ((1 << tileBits) < tileCount)
		{
			tileBits++;
		}
		tileBits = std::min(tileBits, 14);
		const i32 maxPolys = 1 << (22 - tileBits);

		// The bounds of each triangle in x and z, for the choice of what a tile needs.
		std::vector<glm::vec4> triangleBounds(input.Indices.size() / 3);
		for (size_t t = 0; t < triangleBounds.size(); t++)
		{
			const glm::vec3& a = input.Vertices[input.Indices[t * 3 + 0]];
			const glm::vec3& b = input.Vertices[input.Indices[t * 3 + 1]];
			const glm::vec3& c = input.Vertices[input.Indices[t * 3 + 2]];
			triangleBounds[t] = glm::vec4(std::min({ a.x, b.x, c.x }), std::min({ a.z, b.z, c.z }), std::max({ a.x, b.x, c.x }), std::max({ a.z, b.z, c.z }));
		}

		BakeContext context;
		NavMeshData mesh;
		mesh.Build = input.Build;
		mesh.InputHash = nav_input_hash(input);
		mesh.Origin = input.BoundsMin;
		mesh.TileWidth = grid.TileWorld;
		mesh.TileHeight = grid.TileWorld;
		mesh.MaxTiles = tileCount;
		mesh.MaxPolys = maxPolys;

		i32 done = 0;
		for (int z = 0; z < grid.TilesZ; z++)
		{
			for (int x = 0; x < grid.TilesX; x++)
			{
				if (cancelled(cancel))
				{
					result.Error = "cancelled";
					return result;
				}
				if (progress)
				{
					progress(static_cast<float>(done) / static_cast<float>(tileCount), "Building tiles");
				}
				done++;

				TileResult tile = build_tile(context, input, triangleBounds, grid, x, z, maxPolys, cancel);
				if (!tile.Ok)
				{
					result.Error = tile.Error;
					return result;
				}
				if (cancelled(cancel))
				{
					result.Error = "cancelled";
					return result;
				}
				if (tile.Data.empty())
				{
					continue;
				}
				result.PolyCount += tile.PolyCount;
				result.VertCount += tile.VertCount;
				for (u32 i = 0; i < NAV_AREA_COUNT; i++)
				{
					result.AreaPolyCount[i] += tile.AreaPolyCount[i];
				}
				NavTileBlob blob;
				blob.Data = std::move(tile.Data);
				mesh.Tiles.push_back(std::move(blob));
			}
		}

		if (mesh.Tiles.empty())
		{
			result.Error = "nothing in the volume is walkable for an agent of this size";
			return result;
		}

		result.TileCount = static_cast<u32>(mesh.Tiles.size());
		result.Mesh = std::move(mesh);
		result.Ok = true;
		result.Error.clear();
		result.Seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
		if (progress)
		{
			progress(1.0f, "Done");
		}
		return result;
	}

	bool write_nav_file(const std::filesystem::path& path, const NavMeshData& mesh, std::string& error)
	{
		if (!mesh.valid())
		{
			error = "the navmesh is not valid";
			return false;
		}
		const std::vector<std::byte> bytes = nav_mesh_to_bytes(mesh);
		return write_file_atomic(path, bytes.data(), bytes.size(), error);
	}
}
