#pragma once

#include <cmath>

#include "asset_data.h"

// Assets the engine makes itself, with a fixed reference: they are not files, the render thread's resource cache holds them in memory
// (GpuResourceCache registers them when it is made), and the main thread only names them.
namespace wiwa::builtin_assets
{
	// A unit sphere (radius PREVIEW_SPHERE_RADIUS, centered at the origin) for showing a material, with no material of its own.
	inline const AssetRef PREVIEW_SPHERE{ AssetGuid{ 0x5b0e1f6c3a9d4e21ull, 0x8c72d0a45f13b6e9ull }, 0 };
	inline constexpr float PREVIEW_SPHERE_RADIUS = 1.0f;

	// 48 segments around, 24 rings: smooth enough to read a highlight and a normal map. Counter-clockwise triangles seen from outside, v down.
	inline MeshData make_preview_sphere()
	{
		constexpr u32 SEGMENTS = 48;
		constexpr u32 RINGS = 24;
		constexpr float PI = 3.14159265358979f;

		MeshData mesh;
		mesh.Name = "preview sphere";
		for (u32 ring = 0; ring <= RINGS; ring++)
		{
			const float v = static_cast<float>(ring) / static_cast<float>(RINGS);
			const float polar = v * PI;
			for (u32 segment = 0; segment <= SEGMENTS; segment++)
			{
				const float u = static_cast<float>(segment) / static_cast<float>(SEGMENTS);
				const float azimuth = u * 2.0f * PI;
				const float nx = std::sin(polar) * std::cos(azimuth);
				const float ny = std::cos(polar);
				const float nz = std::sin(polar) * std::sin(azimuth);
				MeshVertex vertex;
				vertex.Position[0] = nx * PREVIEW_SPHERE_RADIUS;
				vertex.Position[1] = ny * PREVIEW_SPHERE_RADIUS;
				vertex.Position[2] = nz * PREVIEW_SPHERE_RADIUS;
				vertex.Normal[0] = nx;
				vertex.Normal[1] = ny;
				vertex.Normal[2] = nz;
				vertex.Uv[0] = u;
				vertex.Uv[1] = v;
				mesh.Vertices.push_back(vertex);
			}
		}
		const u32 stride = SEGMENTS + 1;
		for (u32 ring = 0; ring < RINGS; ring++)
		{
			for (u32 segment = 0; segment < SEGMENTS; segment++)
			{
				const u32 a = ring * stride + segment;
				const u32 b = a + stride;
				// azimuth runs from +X toward +Z, polar from +Y down: (a, a + 1, b) faces outward
				for (const u32 index : { a, a + 1, b, a + 1, b + 1, b })
				{
					mesh.Indices.push_back(index);
				}
			}
		}

		MeshPrimitive primitive;
		primitive.IndexCount = static_cast<u32>(mesh.Indices.size());
		primitive.VertexCount = static_cast<u32>(mesh.Vertices.size());
		primitive.BoundsMin = glm::vec3(-PREVIEW_SPHERE_RADIUS);
		primitive.BoundsMax = glm::vec3(PREVIEW_SPHERE_RADIUS);
		mesh.Primitives.push_back(primitive);
		mesh.BoundsMin = primitive.BoundsMin;
		mesh.BoundsMax = primitive.BoundsMax;
		return mesh;
	}
}
