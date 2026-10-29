#pragma once

#include <assets/asset_data.h>

// Small meshes for the render checks.
namespace wiwa
{
	// An axis-aligned box of the given half size centered at the origin: 24 vertices (4 per face), 36 indices, one primitive.
	inline MeshData make_test_box(glm::vec3 halfSize, const AssetRef& material)
	{
		MeshData mesh;
		mesh.Name = "test box";

		struct Face
		{
			glm::vec3 Normal;
			glm::vec3 Right;
			glm::vec3 Up;
		};
		const Face faces[6] = {
			{ { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } },   // +Z
			{ { 0, 0, -1 }, { -1, 0, 0 }, { 0, 1, 0 } }, // -Z
			{ { 1, 0, 0 }, { 0, 0, -1 }, { 0, 1, 0 } },  // +X
			{ { -1, 0, 0 }, { 0, 0, 1 }, { 0, 1, 0 } },  // -X
			{ { 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, -1 } },  // +Y
			{ { 0, -1, 0 }, { 1, 0, 0 }, { 0, 0, 1 } },  // -Y
		};

		for (const Face& face : faces)
		{
			const u32 first = static_cast<u32>(mesh.Vertices.size());
			const glm::vec2 corners[4] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
			for (const glm::vec2& corner : corners)
			{
				const glm::vec3 position = (face.Normal + face.Right * corner.x + face.Up * corner.y) * halfSize;
				MeshVertex vertex;
				vertex.Position[0] = position.x;
				vertex.Position[1] = position.y;
				vertex.Position[2] = position.z;
				vertex.Normal[0] = face.Normal.x;
				vertex.Normal[1] = face.Normal.y;
				vertex.Normal[2] = face.Normal.z;
				vertex.Uv[0] = corner.x * 0.5f + 0.5f;
				vertex.Uv[1] = 0.5f - corner.y * 0.5f;
				mesh.Vertices.push_back(vertex);
			}
			for (u32 index : { 0u, 1u, 2u, 0u, 2u, 3u })
			{
				mesh.Indices.push_back(first + index);
			}
		}

		MeshPrimitive primitive;
		primitive.IndexCount = static_cast<u32>(mesh.Indices.size());
		primitive.VertexCount = static_cast<u32>(mesh.Vertices.size());
		primitive.Material = material;
		primitive.BoundsMin = -halfSize;
		primitive.BoundsMax = halfSize;
		mesh.Primitives.push_back(primitive);
		mesh.BoundsMin = -halfSize;
		mesh.BoundsMax = halfSize;
		return mesh;
	}

	inline MeshData make_test_cube(const AssetRef& material)
	{
		return make_test_box(glm::vec3(1.0f), material);
	}
}
