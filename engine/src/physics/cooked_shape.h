#pragma once

#include <string>
#include <vector>

#include <assets/asset_guid.h>
#include <math/math.h>

namespace wiwa
{
	enum class CookedShapeKind : u32 { ConvexHull, Mesh };

	// A collision shape of a mesh, cooked by the editor: Jolt's binary state of a convex hull or of a triangle mesh with its BVH. A
	// PhysicsShape asset. Opaque to everything but physics/shape_builder.cpp (restoring it) and the cooker functions below.
	struct CookedShapeData
	{
		CookedShapeKind Kind = CookedShapeKind::ConvexHull;
		std::vector<u8> Bytes{};
	};

	constexpr u32 COOKED_SHAPE_VERSION = 1;

	// The cooked shapes of a model's mesh are sub-assets of the model with these local ids added to the mesh's, so that a Collider's Mesh
	// reference (the mesh) names its shape too. Local ids the importer hands out stay below them.
	constexpr u64 PHYSICS_HULL_LOCAL_ID_OFFSET = 1ull << 24;
	constexpr u64 PHYSICS_MESH_LOCAL_ID_OFFSET = 2ull << 24;

	// The PhysicsShape asset that belongs to the mesh.
	AssetRef cooked_shape_ref(AssetRef mesh, CookedShapeKind kind);

	// Cooking (editor side; uses Jolt, so the caller needs nothing of it). A hull from fewer than four points that span a volume, or a
	// mesh without a triangle, fails with the reason in error.
	bool cook_convex_hull(const std::vector<glm::vec3>& points, CookedShapeData& out, std::string& error);
	bool cook_triangle_mesh(const std::vector<glm::vec3>& positions, const std::vector<u32>& indices, CookedShapeData& out, std::string& error);
}
