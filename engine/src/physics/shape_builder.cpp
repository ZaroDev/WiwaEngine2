#include <pch.h>
#include "shape_builder.h"

#include "physics_material_jolt.h"

#include <algorithm>
#include <cmath>

#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>

namespace wiwa
{
	namespace
	{
		bool finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

		glm::vec3 safe_scale(glm::vec3 scale)
		{
			return finite(scale) ? glm::abs(scale) : glm::vec3(1.0f);
		}

		bool nearly(float a, float b) { return std::abs(a - b) <= 1e-4f * std::max({ 1.0f, std::abs(a), std::abs(b) }); }

		JPH::Quat to_jolt(glm::quat q)
		{
			const float length = glm::length(q);
			if (!(length > 1e-6f) || !std::isfinite(length))
			{
				return JPH::Quat::sIdentity();
			}
			q /= length;
			return JPH::Quat(q.x, q.y, q.z, q.w);
		}
	}

	void ShapeBuilder::warn_once(EntityGuid source, Warning warning, const std::string& text)
	{
		if (m_Warned.insert({ source, static_cast<u32>(warning) }).second)
		{
			WI_PHYSICS_LOG(Warning, "Collider {0}: {1}", source.to_string(), text);
		}
	}

	JPH::RefConst<JPH::Shape> ShapeBuilder::build(const ShapeDesc& desc)
	{
		if (desc.Kind != ShapeKind::Compound)
		{
			return build_part(desc);
		}

		JPH::StaticCompoundShapeSettings compound;
		JPH::RefConst<JPH::Shape> only;
		u32 parts = 0;
		for (const ShapeDesc& child : desc.Children)
		{
			if (child.Kind == ShapeKind::Compound)
			{
				warn_once(child.Source, Warning::Unsupported, "a compound inside a compound is not supported, left out");
				continue;
			}
			if (JPH::RefConst<JPH::Shape> part = build_part(child))
			{
				compound.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), part);
				only = part;
				parts++;
			}
		}

		if (parts <= 1)
		{
			return only; // null when nothing was left
		}

		const JPH::ShapeSettings::ShapeResult result = compound.Create();
		if (result.HasError())
		{
			warn_once(desc.Source, Warning::Failed, std::string("the compound shape could not be made: ") + result.GetError().c_str());
			return nullptr;
		}
		return result.Get();
	}

	JPH::RefConst<JPH::Shape> ShapeBuilder::build_part(const ShapeDesc& desc)
	{
		const glm::vec3 scale = safe_scale(desc.Scale);
		const glm::vec3 size = finite(desc.Size) ? desc.Size : glm::vec3(0.0f);
		bool clamped = false;
		const auto dimension = [&clamped](float value)
		{
			if (value < MIN_SHAPE_SIZE)
			{
				clamped = true;
				return MIN_SHAPE_SIZE;
			}
			return value;
		};

		const auto uniform = [&](float largest)
		{
			if (!nearly(scale.x, scale.y) || !nearly(scale.x, scale.z))
			{
				warn_once(desc.Source, Warning::NonUniformScale, "a sphere or capsule needs a uniform scale; the largest axis (" + std::to_string(largest) + ") is used");
			}
		};

		// the surface of this part: friction and restitution reach the contact through it, the density sets the mass
		PhysicsMaterialData surface = desc.Material;
		if (!physics_material_valid(surface))
		{
			warn_once(desc.Source, Warning::Failed, "the physics material has values that are not allowed, the default is used");
			surface = default_physics_material();
		}
		const JPH::Ref<JPH::PhysicsMaterial> material = new WiwaPhysicsMaterial(surface);

		JPH::Ref<JPH::ConvexShape> convex;
		switch (desc.Kind)
		{
		case ShapeKind::Box:
		{
			const JPH::Vec3 half(dimension(size.x * scale.x), dimension(size.y * scale.y), dimension(size.z * scale.z));
			convex = new JPH::BoxShape(half, std::min(JPH::cDefaultConvexRadius, 0.5f * half.ReduceMin()), material);
			break;
		}
		case ShapeKind::Sphere:
		{
			const float largest = std::max({ scale.x, scale.y, scale.z });
			uniform(largest);
			convex = new JPH::SphereShape(dimension(size.x * largest), material);
			break;
		}
		case ShapeKind::Capsule:
		{
			const float largest = std::max({ scale.x, scale.y, scale.z });
			uniform(largest);
			convex = new JPH::CapsuleShape(dimension(size.y * largest), dimension(size.x * largest), material);
			break;
		}
		case ShapeKind::Cylinder:
		{
			const float radial = std::max(scale.x, scale.z);
			if (!nearly(scale.x, scale.z))
			{
				warn_once(desc.Source, Warning::NonUniformScale, "a cylinder needs the same x and z scale; the larger (" + std::to_string(radial) + ") is used");
			}
			const float halfHeight = dimension(size.y * scale.y);
			const float radius = dimension(size.x * radial);
			convex = new JPH::CylinderShape(halfHeight, radius, std::min(JPH::cDefaultConvexRadius, 0.5f * std::min(halfHeight, radius)), material);
			break;
		}
		case ShapeKind::Placeholder:
			warn_once(desc.Source, Warning::Unsupported, "this shape kind is not supported yet (ConvexHull and Mesh need cooked shapes); it does not collide");
			return nullptr;
		case ShapeKind::Compound:
			return nullptr;
		}

		if (clamped)
		{
			warn_once(desc.Source, Warning::Clamped, "a size below " + std::to_string(MIN_SHAPE_SIZE) + " m was raised to it");
		}

		const float density = surface.Density;
		convex->SetDensity(std::max(density, 1e-3f));

		const glm::vec3 offset = finite(desc.Offset) ? desc.Offset : glm::vec3(0.0f);
		const JPH::Quat rotation = to_jolt(desc.Rotation);
		if (glm::length(offset) < 1e-6f && rotation.IsClose(JPH::Quat::sIdentity(), 1e-10f))
		{
			return JPH::RefConst<JPH::Shape>(convex.GetPtr());
		}
		return new JPH::RotatedTranslatedShape(JPH::Vec3(offset.x, offset.y, offset.z), rotation, convex);
	}
}
