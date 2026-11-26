#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include <physics/physics_material_asset.h>

// The source format of a physics material (.wphysmat): JSON with "friction", "restitution", "density" (numbers, at least 0) and
// "frictionCombine", "restitutionCombine" ("Average", "Min", "Max" or "Multiply"), "tireGrip" (number, at least 0: the grip of a vehicle tire on it). A key that is missing takes the default.
namespace wiwa
{
	const char* combine_mode_name(CombineMode mode);

	// False with a reason in error when the text is not JSON, a value has the wrong type or is below 0, or a combine mode is unknown.
	bool physics_material_from_text(const std::string& text, PhysicsMaterialData& out, std::string& error);

	nlohmann::json physics_material_to_json(const PhysicsMaterialData& data);
}
