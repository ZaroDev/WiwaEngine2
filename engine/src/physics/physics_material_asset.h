#pragma once

#include <math/math.h>

namespace wiwa
{
	// How the values of two touching materials make the value of the contact.
	enum class CombineMode : u32 { Average, Min, Max, Multiply };

	// The surface of a collider, as the PhysicsMaterial asset (.wphysmat) cooks it. Plain data and Jolt-free: the physics thread copies it
	// into the shapes it builds, so it never touches the AssetManager. Density is in kg/m3.
	struct PhysicsMaterialData
	{
		float Friction = 0.5f;
		float Restitution = 0.0f;
		float Density = 1000.0f;
		CombineMode FrictionCombine = CombineMode::Average;
		CombineMode RestitutionCombine = CombineMode::Average;
		float TireGrip = 1.0f; // how much grip a vehicle tire has on it (vehicles.md); 1: the tire model as tuned

		bool operator==(const PhysicsMaterialData&) const = default;
	};

	// What a collider without a material asset (or one that failed to load) uses.
	PhysicsMaterialData default_physics_material();

	// Version of the cooked payload (a u32 first in the blob).
	constexpr u32 PHYSICS_MATERIAL_VERSION = 2; // 2 added TireGrip (a version 1 payload loads with 1)

	// Whether the values are usable: finite, at least 0, combine modes in range. The importer refuses a file that is not, and the
	// loader refuses a payload that is not.
	bool physics_material_valid(const PhysicsMaterialData& data);

	// Jolt combines the materials of the two bodies in a contact into one value, so there is a mode for each of the two. Rule: the
	// mode with the higher priority wins, in the order Multiply > Max > Min > Average.
	CombineMode stronger_combine_mode(CombineMode a, CombineMode b);

	// The value of a contact for two materials' values a and b.
	float combine_values(CombineMode mode, float a, float b);
}
