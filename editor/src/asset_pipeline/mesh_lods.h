#pragma once

#include <assets/asset_data.h>

namespace wiwa
{
	// How levels of detail are baked into a mesh (meshoptimizer's simplifier, over the vertices the mesh already has: a level is another run of
	// indices, MeshPrimitive::Lods).
	struct LodBakeSettings
	{
		u32 Levels = 3;                  // levels besides the full one, 0 to MAX_MESH_LODS - 1; 0 bakes nothing (and clears what the mesh had)
		float Reduction = 0.5f;          // each level keeps this fraction of the triangles of the full mesh to the power of its number: 0.5 gives 50%, 25%, 12.5%
		float MaxError = 0.05f;          // a level may stray from the full surface by this fraction of the primitive's size; levels that cannot reach their triangle count within it keep what they reached
		u32 MinTriangles = 128;          // a primitive with fewer triangles than this gets no levels

		bool operator==(const LodBakeSettings&) const = default;
	};

	struct LodBakeResult
	{
		u32 Primitives = 0;      // primitives that got at least one level
		u32 Levels = 0;          // levels written, over all primitives
		u32 TrianglesBefore = 0; // of the full level, of the primitives that got levels
		u32 TrianglesLast = 0;   // of the coarsest level of those primitives
	};

	// Removes the levels of every primitive (the indices they used are dropped) and bakes new ones. Level 0 is never touched. Returns what was made.
	LodBakeResult bake_mesh_lods(MeshData& mesh, const LodBakeSettings& settings);

	// Back to the full level only.
	void clear_mesh_lods(MeshData& mesh);
}
