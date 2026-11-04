#pragma once

#include <set>
#include <utility>

#include "jolt_config.h"
#include <Jolt/Physics/Collision/PhysicsMaterial.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>

#include "physics_commands.h"

namespace wiwa
{
	// Physics-thread side: builds the Jolt shape of a ShapeDesc. The world scale is applied (box: per axis; sphere and capsule: the
	// largest axis, with one warning when the scale is not uniform; cylinder: the largest of x and z for the radius, y for the height),
	// every dimension is clamped to MIN_SHAPE_SIZE, and Offset and Rotation place the shape in the body's frame. A collider the
	// builder cannot make (a ConvexHull or Mesh whose cooked shape is not there) is left out; a body whose shape has nothing
	// left gets a null shape, and the world makes it a non-colliding placeholder.
	class ShapeBuilder
	{
	public:
		static constexpr float MIN_SHAPE_SIZE = 0.01f;

		// Null when nothing collidable is left.
		JPH::RefConst<JPH::Shape> build(const ShapeDesc& desc);

	private:
		enum class Warning : u32 { NonUniformScale, Clamped, Unsupported, Failed };

		JPH::RefConst<JPH::Shape> build_part(const ShapeDesc& desc);
		JPH::RefConst<JPH::Shape> build_cooked(const ShapeDesc& desc, const PhysicsMaterialData& surface, const JPH::PhysicsMaterial* material);
		void warn_once(EntityGuid source, Warning warning, const std::string& text);

		std::set<std::pair<EntityGuid, u32>> m_Warned{}; // (collider entity, warning): each is logged once per world
	};
}
