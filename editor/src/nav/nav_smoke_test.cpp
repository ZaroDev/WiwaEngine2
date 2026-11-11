#include <editor_common.h>
#include "nav_smoke_test.h"

#include <cstring>

#include <nav/nav_mesh_asset.h>

namespace wiwa
{
	namespace
	{
		struct Check
		{
			u32 Failures = 0;

			void operator()(bool condition, const std::string& what)
			{
				if (!condition)
				{
					Failures++;
					WI_NAV_LOG(Error, "Nav smoke test FAILED: {0}", what);
				}
			}
		};

		NavMeshData sample_mesh()
		{
			NavMeshData data;
			data.Build.CellSize = 0.5f;
			data.Build.AgentRadius = 0.25f;
			data.InputHash = 0xABCD;
			data.Origin = glm::vec3(-10.0f, 0.0f, -10.0f);
			data.TileWidth = 32.0f;
			data.TileHeight = 32.0f;
			data.MaxTiles = 4;
			data.MaxPolys = 64;
			for (int tile = 0; tile < 2; tile++)
			{
				NavTileBlob blob;
				for (int i = 0; i < 16; i++)
				{
					blob.Data.push_back(static_cast<std::byte>(tile * 16 + i + 1));
				}
				data.Tiles.push_back(std::move(blob));
			}
			return data;
		}

		void wnav_format(Check& check)
		{
			const NavMeshData data = sample_mesh();
			check(data.valid(), "wnav: the sample mesh is valid");

			const std::vector<std::byte> bytes = nav_mesh_to_bytes(data);
			NavMeshData back;
			std::string error;
			const bool ok = nav_mesh_from_bytes(bytes, back, error);
			check(ok, "wnav: a mesh reads back (" + error + ")");
			check(ok && back == data, "wnav: the mesh read back equals the one written (settings, hash, tiles)");
			check(ok && back.InputHash == 0xABCD && back.Build.CellSize == 0.5f, "wnav: the hash and the build settings survive");

			std::vector<std::byte> wrongMagic = bytes;
			wrongMagic[0] = static_cast<std::byte>(~static_cast<unsigned>(wrongMagic[0]));
			NavMeshData rejected;
			error.clear();
			check(!nav_mesh_from_bytes(wrongMagic, rejected, error) && !error.empty(), "wnav: a wrong magic is refused with a reason");

			std::vector<std::byte> truncated(bytes.begin(), bytes.begin() + bytes.size() / 2);
			error.clear();
			check(!nav_mesh_from_bytes(truncated, rejected, error) && !error.empty(), "wnav: a truncated file is refused with a reason");

			std::vector<std::byte> trailing = bytes;
			trailing.push_back(std::byte{ 0 });
			error.clear();
			check(!nav_mesh_from_bytes(trailing, rejected, error), "wnav: trailing bytes are refused");

			NavMeshData empty = data;
			empty.Tiles.clear();
			error.clear();
			check(!empty.valid() && !nav_mesh_from_bytes(nav_mesh_to_bytes(empty), rejected, error), "wnav: a mesh without tiles is refused");
		}
	}

	bool run_nav_smoke_test(AssetPipelineModule& pipeline)
	{
		(void)pipeline;
		Check check;

		wnav_format(check);

		if (check.Failures == 0)
		{
			WI_NAV_LOG(Info, "Nav smoke test passed");
		}
		return check.Failures == 0;
	}
}
