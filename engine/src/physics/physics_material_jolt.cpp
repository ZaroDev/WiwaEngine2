#include <pch.h>
#include "physics_material_jolt.h"

#include <cstring>

#include <Jolt/Physics/Collision/Shape/Shape.h>

namespace wiwa
{
	const char* const WiwaPhysicsMaterial::kName = "WiwaPhysicsMaterial";

	PhysicsMaterialData material_of(const JPH::Body& body, const JPH::SubShapeID& subShape)
	{
		const JPH::PhysicsMaterial* material = body.GetShape()->GetMaterial(subShape);
		if (material != nullptr && material->GetDebugName() == WiwaPhysicsMaterial::kName)
		{
			return static_cast<const WiwaPhysicsMaterial*>(material)->data();
		}
		return default_physics_material();
	}

	float combine_friction(const JPH::Body& body1, const JPH::SubShapeID& subShape1, const JPH::Body& body2, const JPH::SubShapeID& subShape2)
	{
		const PhysicsMaterialData a = material_of(body1, subShape1);
		const PhysicsMaterialData b = material_of(body2, subShape2);
		return combine_values(stronger_combine_mode(a.FrictionCombine, b.FrictionCombine), a.Friction, b.Friction);
	}

	float combine_restitution(const JPH::Body& body1, const JPH::SubShapeID& subShape1, const JPH::Body& body2, const JPH::SubShapeID& subShape2)
	{
		const PhysicsMaterialData a = material_of(body1, subShape1);
		const PhysicsMaterialData b = material_of(body2, subShape2);
		return combine_values(stronger_combine_mode(a.RestitutionCombine, b.RestitutionCombine), a.Restitution, b.Restitution);
	}
}
