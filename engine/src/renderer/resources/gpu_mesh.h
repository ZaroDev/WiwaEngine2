#pragma once

#include <vector>

#include <math/math.h>

namespace wiwa
{
	constexpr u32 GPU_MAX_LODS = 4; // MAX_MESH_LODS of the asset

	// One level of detail of a primitive: another run of indices over the same vertices.
	struct GpuLod
	{
		u32 FirstIndex = 0; // in the page's index buffer
		u32 IndexCount = 0;
		float Error = 0.0f; // how far this level may stray from level 0, in mesh space
	};

	// One run of triangles of a mesh that share a material, as drawn: an indexed draw into the page's shared buffers.
	struct GpuPrimitive
	{
		u32 FirstIndex = 0; // in the page's index buffer (already offset by where the mesh was placed): level 0, as Lods[0]
		u32 IndexCount = 0;
		i32 BaseVertex = 0; // in the page's vertex buffer
		u32 MaterialSlot = 0; // of the primitive's own material; an instance may override it
		glm::vec3 BoundsMin{ 0.0f };
		glm::vec3 BoundsMax{ 0.0f };
		u32 LodCount = 1;
		GpuLod Lods[GPU_MAX_LODS]{};
	};

	// A mesh living in the cache's shared vertex and index buffers. Valid until the mesh is re-imported (then it is replaced
	// in place, one frame later than the file changed) or the cache is destroyed.
	struct GpuMesh
	{
		u32 Page = 0; // which pair of shared buffers (GpuResourceCache::vertex_buffer / index_buffer)
		std::vector<GpuPrimitive> Primitives{};
		glm::vec3 BoundsMin{ 0.0f };
		glm::vec3 BoundsMax{ 0.0f };
		bool Skinned = false;  // its skin vertices are on the page's skin buffer (GpuResourceCache::skin_buffer)
		u32 JointCount = 0;    // the palette needs this many joints: the highest joint a vertex is weighted to, plus one
	};
}
