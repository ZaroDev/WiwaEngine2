#include <pch.h>
#include "cooked_shape.h"
#include "jolt_config.h"
#include "jolt_runtime.h"

#include <cmath>
#include <sstream>

#include <Jolt/Core/StreamWrapper.h>
#include <Jolt/Physics/Collision/PhysicsMaterial.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>

#include <assets/wasset_io.h>

namespace wiwa
{
	AssetRef cooked_shape_ref(AssetRef mesh, CookedShapeKind kind)
	{
		mesh.LocalId += kind == CookedShapeKind::ConvexHull ? PHYSICS_HULL_LOCAL_ID_OFFSET : PHYSICS_MESH_LOCAL_ID_OFFSET;
		return mesh;
	}

	namespace
	{
		bool save(const JPH::Shape& shape, CookedShapeKind kind, CookedShapeData& out, std::string& error)
		{
			std::stringstream stream(std::ios::in | std::ios::out | std::ios::binary);
			JPH::StreamOutWrapper wrapper(stream);
			shape.SaveBinaryState(wrapper);
			if (wrapper.IsFailed())
			{
				error = "the shape could not be saved";
				return false;
			}
			const std::string bytes = stream.str();
			out.Kind = kind;
			out.Bytes.assign(bytes.begin(), bytes.end());
			return true;
		}

		bool finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
	}

	bool cook_convex_hull(const std::vector<glm::vec3>& points, CookedShapeData& out, std::string& error)
	{
		JoltRuntimeScope runtime;
		JPH::Array<JPH::Vec3> vertices;
		vertices.reserve(points.size());
		for (const glm::vec3& point : points)
		{
			if (finite(point))
			{
				vertices.push_back(JPH::Vec3(point.x, point.y, point.z));
			}
		}
		if (vertices.size() < 4)
		{
			error = "a convex hull needs at least four points";
			return false;
		}

		JPH::ConvexHullShapeSettings settings(vertices, 0.0f);
		const JPH::ShapeSettings::ShapeResult result = settings.Create();
		if (result.HasError())
		{
			error = std::string("the convex hull could not be made: ") + result.GetError().c_str();
			return false;
		}
		return save(*result.Get(), CookedShapeKind::ConvexHull, out, error);
	}

	bool cook_triangle_mesh(const std::vector<glm::vec3>& positions, const std::vector<u32>& indices, CookedShapeData& out, std::string& error)
	{
		JoltRuntimeScope runtime;
		JPH::VertexList vertices;
		vertices.reserve(positions.size());
		for (const glm::vec3& position : positions)
		{
			vertices.push_back(JPH::Float3(finite(position) ? position.x : 0.0f, finite(position) ? position.y : 0.0f, finite(position) ? position.z : 0.0f));
		}

		JPH::IndexedTriangleList triangles;
		triangles.reserve(indices.size() / 3);
		for (size_t i = 0; i + 2 < indices.size(); i += 3)
		{
			const u32 a = indices[i], b = indices[i + 1], c = indices[i + 2];
			if (a >= positions.size() || b >= positions.size() || c >= positions.size() || a == b || b == c || a == c)
			{
				continue; // a triangle Jolt would refuse
			}
			triangles.push_back(JPH::IndexedTriangle(a, b, c, 0));
		}
		if (triangles.empty())
		{
			error = "the mesh has no triangle";
			return false;
		}

		// one material slot: the builder puts the collider's material in it
		JPH::PhysicsMaterialList materials;
		materials.push_back(JPH::PhysicsMaterial::sDefault);
		JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles), std::move(materials));
		const JPH::ShapeSettings::ShapeResult result = settings.Create();
		if (result.HasError())
		{
			error = std::string("the triangle mesh could not be made: ") + result.GetError().c_str();
			return false;
		}
		return save(*result.Get(), CookedShapeKind::Mesh, out, error);
	}

	void archive(BinaryArchive& ar, CookedShapeData& v)
	{
		u32 version = COOKED_SHAPE_VERSION;
		ar.value(version);
		if (ar.reading() && ar.ok() && version != COOKED_SHAPE_VERSION)
		{
			ar.fail();
			return;
		}
		ar.value(v.Kind);
		if (ar.reading() && ar.ok() && static_cast<u32>(v.Kind) > static_cast<u32>(CookedShapeKind::Mesh))
		{
			ar.fail();
			return;
		}
		ar.pod_vector(v.Bytes);
		if (ar.reading() && ar.ok() && v.Bytes.empty())
		{
			ar.fail(); // an empty shape would be a body that silently collides with nothing
		}
	}
}
