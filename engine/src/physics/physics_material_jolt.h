#pragma once

#include "jolt_config.h"
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Collision/PhysicsMaterial.h>

#include "physics_material_asset.h"

namespace wiwa
{
	// A Jolt material that holds the data of a PhysicsMaterial asset. The shape builder gives one to every shape it makes, and the combine
	// callbacks read it back from the two shapes of a contact. Physics-thread side; the main thread only ever sees PhysicsMaterialData.
	class WiwaPhysicsMaterial final : public JPH::PhysicsMaterial
	{
	public:
		// One address for every translation unit; the callbacks tell our materials from Jolt's by it.
		static const char* const kName;

		explicit WiwaPhysicsMaterial(const PhysicsMaterialData& data) : m_Data(data) {}

		virtual const char* GetDebugName() const override { return kName; }
		const PhysicsMaterialData& data() const { return m_Data; }

	private:
		PhysicsMaterialData m_Data{};
	};

	// The material of the shape a contact touched (the sub shape of a compound has its own); the default for a shape without one.
	PhysicsMaterialData material_of(const JPH::Body& body, const JPH::SubShapeID& subShape);

	// The contact's friction and restitution from the two materials, installed on the PhysicsSystem with SetCombineFriction and
	// SetCombineRestitution (plain function pointers, so no state). Jolt combines two materials into one value, each material has its own
	// mode: the higher-priority mode wins, in the order Multiply > Max > Min > Average (so a Multiply material multiplies, whatever it
	// touches). With two Average materials the value is their mean.
	float combine_friction(const JPH::Body& body1, const JPH::SubShapeID& subShape1, const JPH::Body& body2, const JPH::SubShapeID& subShape2);
	float combine_restitution(const JPH::Body& body1, const JPH::SubShapeID& subShape1, const JPH::Body& body2, const JPH::SubShapeID& subShape2);
}
