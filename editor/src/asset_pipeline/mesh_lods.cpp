#include <editor_common.h>
#include "mesh_lods.h"

#include <meshoptimizer.h>

#include <algorithm>
#include <cmath>

namespace wiwa
{
	void clear_mesh_lods(MeshData& mesh)
	{
		// the full levels sit first (the importers write them in primitive order); the levels were appended after the last of them
		u64 fullEnd = 0;
		for (MeshPrimitive& primitive : mesh.Primitives)
		{
			fullEnd = std::max<u64>(fullEnd, static_cast<u64>(primitive.FirstIndex) + primitive.IndexCount);
			primitive.LodCount = 1;
			primitive.Lods[0] = MeshLod{ primitive.FirstIndex, primitive.IndexCount, 0.0f };
			for (u32 level = 1; level < MAX_MESH_LODS; level++)
			{
				primitive.Lods[level] = MeshLod{};
			}
		}
		if (fullEnd < mesh.Indices.size())
		{
			mesh.Indices.resize(static_cast<size_t>(fullEnd));
		}
	}

	LodBakeResult bake_mesh_lods(MeshData& mesh, const LodBakeSettings& settings)
	{
		LodBakeResult result;
		clear_mesh_lods(mesh);

		const u32 levels = std::min(settings.Levels, MAX_MESH_LODS - 1);
		if (levels == 0)
		{
			return result;
		}
		const float reduction = std::clamp(settings.Reduction, 0.1f, 0.9f);

		// The levels are appended after everything the mesh has, so collect them first (the loop reads the index array it appends to).
		std::vector<u32> appended;
		const u32 baseIndexCount = static_cast<u32>(mesh.Indices.size());

		static_assert(offsetof(MeshVertex, Uv) == offsetof(MeshVertex, Normal) + 3 * sizeof(float), "normal and uv are one run of five floats");
		const float weights[5] = { 0.6f, 0.6f, 0.6f, 0.3f, 0.3f };

		for (MeshPrimitive& primitive : mesh.Primitives)
		{
			const u32 triangles = primitive.IndexCount / 3;
			if (primitive.IndexCount % 3 != 0 || triangles < settings.MinTriangles || primitive.FirstIndex + static_cast<u64>(primitive.IndexCount) > baseIndexCount)
			{
				continue;
			}

			// the vertices the primitive's indices reach (they are relative to BaseVertex)
			u32 vertexCount = primitive.VertexCount;
			const u32* full = mesh.Indices.data() + primitive.FirstIndex;
			if (vertexCount == 0)
			{
				for (u32 i = 0; i < primitive.IndexCount; i++)
				{
					vertexCount = std::max(vertexCount, full[i] + 1);
				}
			}
			if (primitive.BaseVertex < 0 || static_cast<u64>(primitive.BaseVertex) + vertexCount > mesh.Vertices.size())
			{
				continue;
			}
			const MeshVertex* vertices = mesh.Vertices.data() + primitive.BaseVertex;
			const float* positions = vertices[0].Position;

			// Vertices that sit at the same place (the importers split them at uv and normal seams) are one vertex to the simplifier, so seams do not tear.
			std::vector<unsigned int> remap(vertexCount);
			meshopt_generatePositionRemap(remap.data(), positions, vertexCount, sizeof(MeshVertex));
			std::vector<unsigned int> welded(primitive.IndexCount);
			bool valid = true;
			for (u32 i = 0; i < primitive.IndexCount; i++)
			{
				valid = valid && full[i] < vertexCount;
				welded[i] = valid ? remap[full[i]] : 0;
			}
			if (!valid)
			{
				continue;
			}

			const float extent = meshopt_simplifyScale(positions, vertexCount, sizeof(MeshVertex));
			const float errorLimit = std::max(extent * settings.MaxError, 1e-6f);

			u32 previousCount = primitive.IndexCount;
			float previousError = 0.0f;
			u32 made = 0;
			for (u32 level = 1; level <= levels; level++)
			{
				const size_t target = std::max<size_t>(static_cast<size_t>(static_cast<double>(primitive.IndexCount) * std::pow(reduction, level)) / 3 * 3, 12);
				std::vector<unsigned int> simplified(primitive.IndexCount);
				float error = 0.0f;
				const size_t count = meshopt_simplifyWithAttributes(simplified.data(), welded.data(), welded.size(), positions, vertexCount, sizeof(MeshVertex),
					vertices[0].Normal, sizeof(MeshVertex), weights, 5, nullptr, target, errorLimit, meshopt_SimplifyErrorAbsolute | meshopt_SimplifyPrune, &error);
				if (count < 3 || count + primitive.IndexCount / 20 >= previousCount)
				{
					break; // the simplifier cannot get any lower within the error: no more levels
				}
				simplified.resize(count);
				meshopt_optimizeVertexCache(simplified.data(), simplified.data(), simplified.size(), vertexCount);

				previousError = std::max(error, previousError + extent * 1e-4f); // errors grow with the level, so a level is never chosen over a finer one that fits
				primitive.Lods[level] = MeshLod{ baseIndexCount + static_cast<u32>(appended.size()), static_cast<u32>(count), previousError };
				appended.insert(appended.end(), simplified.begin(), simplified.end());
				previousCount = static_cast<u32>(count);
				made = level;
				result.Levels++;
				if (level == 1)
				{
					result.Primitives++;
					result.TrianglesBefore += triangles;
				}
			}
			primitive.LodCount = made + 1;
			if (made > 0)
			{
				result.TrianglesLast += primitive.Lods[made].IndexCount / 3;
			}
		}

		mesh.Indices.insert(mesh.Indices.end(), appended.begin(), appended.end());
		return result;
	}
}
